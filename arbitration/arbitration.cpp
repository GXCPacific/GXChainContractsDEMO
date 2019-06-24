#include <graphenelib/contract.hpp>
#include <graphenelib/contract_asset.hpp>
#include <graphenelib/dispatcher.hpp>
#include <graphenelib/global.h>
#include <graphenelib/multi_index.hpp>
#include <graphenelib/print.hpp>
#include <graphenelib/system.h>
#include <graphenelib/types.h>

using namespace graphene;

static const int64_t editable_time_limit = 3600 * 24 * 3;
static const int64_t minimum_expiration_time = 3600 * 24 * 4; 

class arbitration : public contract
{
    public:

    arbitration(uint64_t id)
        :contract(id)
        , arbitrations(_self, _self)
        , verdicts(_self, _self)
        {

        }

    /* 
     * Commit a request for arbitration
     * @param arbitration_name - name of the arbitration to be commit
     * @param content - Proof-providing content of this arbitration by claimant
     * @param respondent_account_name - account name of respondent
     * @param associative_tx - transaction id of associative transaction with the arbitration
     * @param expiration_time - expiration time of the arbitration
     */
    //@abi action
    void commitarb(graphenelib::name arbitration_name,std::string content,std::string respondent_account_name,std::string associative_tx,int64_t expiration_time){
        
        
        uint64_t sender = get_trx_sender();
        int64_t now = get_head_block_time();

        graphene_assert(expiration_time > now + minimum_expiration_time,"You need to reserve enough time for the arbitrator to arbitrate!");

        graphene_assert(content.size() < 32768,"Content should be shorter then 32768 bytes. ");

        uint64_t respondent_account = get_account_id(respondent_account_name.c_str(),respondent_account_name.length());

        graphene_assert(respondent_account != -1 , "Respondent account do not exists, please make sure ");

        auto iter = arbitrations.find(arbitration_name);

        graphene_assert(iter == arbitrations.end(),"This arbitration already exists! Please change you arbitration name ,or update the exist arbitration if you owner this arbitration.");

        //store arbitration request info into multi_index
        arbitrations.emplace(sender,[&](auto &o){
            o.arbitration_name = arbitration_name;
            o.claimant = sender;
            o.respondent_account = respondent_account;
            o.associative_tx = associative_tx;
            o.proof_content = content;
            o.create_time = now;
            o.expiration_time = expiration_time;
        });

        verdicts.emplace(sender,[&](auto &o){
            o.arbitration_name = arbitration_name;
        });


    }


    /* 
     * Update a request for arbitration
     * @param arbitration_name - name of the arbitration to be update
     * @param content - Proof-providing content of this arbitration by claimant
     * @param expiration_time - expiration time of the arbitration
     */
    //@abi action
    void updatearb(graphenelib::name arbitration_name,std::string content,int64_t expiration_time){
        uint64_t sender = get_trx_sender();
        int64_t now = get_head_block_time();

        auto iter = arbitrations.find(arbitration_name);

        graphene_assert(iter != arbitrations.end(),"Arbitration not exists! Please update an exists arbitration or commit a new arbitration.");

        graphene_assert(content.size() < 32768,"Content should be shorter then 32768 bytes. ");

        graphene_assert(sender == iter->claimant,"It's not your arbitration! You can only update your arbitrations.");

        graphene_assert(now < iter->create_time + editable_time_limit,"This arbitration can not be update now, you must update arbitration in the prescribed time!");

        graphene_assert(expiration_time > iter->create_time + minimum_expiration_time,"You need to reserve enough time for the arbitrator to arbitrate!");

        arbitrations.modify(iter,sender,[&](auto &o){
            o.proof_content = content;
            o.expiration_time = expiration_time;
        });

    }

    /* 
     * Response to a request for arbitration
     * @param arbitration_name - name of the arbitration to be response
     * @param response - Proof-providing content of this arbitration by respondent
     * 
     */
    //@abi action
    void responsearb(graphenelib::name arbitration_name,std::string response){
        uint64_t sender = get_trx_sender();
        int64_t now = get_head_block_time();

        auto iter = arbitrations.find(arbitration_name);

        graphene_assert(iter != arbitrations.end(),"Arbitration not exists! Please response to an exists arbitration.");

        graphene_assert(iter->respondent_account == sender,"You are not the respondent of this arbitration,so you don't have permission to response for this arbitration!");

        graphene_assert(response.size() < 32768,"Response content should be shorter then 32768 bytes. ");

        graphene_assert(now < iter->create_time + editable_time_limit,"This arbitration can not be responsed now,you must response arbitration in the prescribed time!");

        arbitrations.modify(iter,sender,[&](auto &o){
            o.response = response;
        });
    }

    /* 
     * agree an arbitration
     * @param arbitration_name - name of the arbitration to be agreed
     * 
     */
    //@abi action
    void agreearb(graphenelib::name arbitration_name){
        
        uint64_t sender = get_trx_sender();
        int64_t now = get_head_block_time();

        auto arb_iter =  arbitrations.find(arbitration_name);

        graphene_assert(arb_iter != arbitrations.end(),"This arbitration request not exists!");

        graphene_assert(now < arb_iter->expiration_time,"This arbitration is out of expiration time!");

        auto verdict_iter = verdicts.find(arbitration_name);

        graphene_assert(verdict_iter != verdicts.end(),"This arbitration request not exists!");

        auto iter = std::find_if(verdict_iter->agree_list.begin(),verdict_iter->agree_list.end(),[&](auto &o){return o.account == sender;});

        graphene_assert(iter == verdict_iter->agree_list.end(),"Already agree!");

        verdicts.modify(verdict_iter,sender,[&](auto &o){
            o.agree_list.push_back(verdict{sender,now});

            auto temp = std::find_if(verdict_iter->disagree_list.begin(),verdict_iter->disagree_list.end(),[&](auto &o){return o.account == sender;});

            if(temp != verdict_iter->disagree_list.end()){
                o.disagree_list.erase(temp);
            }

        });

    }

    /* 
     * disagree an arbitration
     * @param arbitration_name - name of the arbitration to be disagree
     * 
     */
    //@abi action
    void disagreearb(graphenelib::name arbitration_name){
        uint64_t sender = get_trx_sender();
        int64_t now = get_head_block_time();


        auto arb_iter =  arbitrations.find(arbitration_name);

        graphene_assert(arb_iter != arbitrations.end(),"This arbitration request not exists!");

        graphene_assert(now < arb_iter->expiration_time,"This arbitration is out of expiration time!");

        auto verdict_iter = verdicts.find(arbitration_name);

        graphene_assert(verdict_iter != verdicts.end(),"This arbitration request not exists!");

        auto iter = std::find_if(verdict_iter->disagree_list.begin(),verdict_iter->disagree_list.end(),[&](auto &o){return o.account == sender;});

        graphene_assert(iter == verdict_iter->disagree_list.end(),"Already disagree!");

        verdicts.modify(verdict_iter,sender,[&](auto &o){

            o.disagree_list.push_back(verdict{sender,now});

            auto temp = std::find_if(verdict_iter->agree_list.begin(),verdict_iter->agree_list.end(),[&](auto &o){return o.account == sender;});

            if(temp != verdict_iter->agree_list.end()){
                o.agree_list.erase(temp);
            }

        });
    }

    /* 
     * execute an arbitration
     * @param arbitration_name - name of the arbitration to be executed
     * 
     */
    //@abi action
    void exec(graphenelib::name arbitration_name){

        int64_t now = get_head_block_time();

        auto iter =  arbitrations.find(arbitration_name);

        graphene_assert(iter != arbitrations.end(),"This arbitration request not exists!");

        graphene_assert(now > iter->expiration_time,"This arbitration is waiting for arbitrate and can not be executed now!");

        auto verdict_iter = verdicts.find(arbitration_name);

        graphene_assert(verdict_iter != verdicts.end(),"This arbitration request not exists!");

        uint64_t agree_count = verdict_iter->agree_list.size();
        uint64_t disagree_count = verdict_iter->disagree_list.size();

        print("After a formal hearing of the materials provided by the parties to the arbitration, the arbitrators discussed and voted. The results are as follows:");
        print("This arbitration got ",agree_count," consents and ",disagree_count," disagreements.");

        if(agree_count > disagree_count){
            print("Therefore, the current arbitral tribunal supports the request for arbitration.\n");
        }else{
            print("Therefore, the current arbitral tribunal reject the request for arbitration.\n");
        }

        verdicts.erase(verdict_iter);

        arbitrations.erase(iter);
    }




    private:

    //@abi table arbinfo i64
    struct arbinfo{
        uint64_t arbitration_name;
        uint64_t claimant;
        uint64_t respondent_account;
        std::string associative_tx;
        std::string proof_content;
        std::string response;
        int64_t expiration_time;
        int64_t create_time;

        uint64_t primary_key() const { return arbitration_name; }

        GRAPHENE_SERIALIZE(arbinfo,(arbitration_name)(claimant)(respondent_account)(associative_tx)(proof_content)(response)(expiration_time)(create_time))
    };

    typedef graphene::multi_index<N(arbinfo), arbinfo> arbitration_infos;

    struct verdict{
        uint64_t account;
        int64_t verdict_time;
    };

    //@abi table verdictinfo i64
    struct verdictinfo{
        uint64_t arbitration_name;
        std::vector<verdict> agree_list;
        std::vector<verdict> disagree_list;

        uint64_t primary_key() const { return arbitration_name; }

        GRAPHENE_SERIALIZE(verdictinfo,(arbitration_name)(agree_list)(disagree_list))
    };

    typedef graphene::multi_index<N(verdictinfo), verdictinfo> verdict_infos;

    arbitration_infos arbitrations;
    verdict_infos verdicts;


};

GRAPHENE_ABI(arbitration, (commitarb)(responsearb)(agreearb)(disagreearb)(exec)(updatearb))