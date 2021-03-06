
#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>
namespace ripple {
void appendOfferJson (std::shared_ptr<SLE const> const& offer,
                      Json::Value& offers)
{
    STAmount dirRate = amountFromQuality (
          getQuality (offer->getFieldH256 (sfBookDirectory)));
    Json::Value& obj (offers.append (Json::objectValue));
    offer->getFieldAmount (sfTakerPays).setJson (obj[jss::taker_pays]);
    offer->getFieldAmount (sfTakerGets).setJson (obj[jss::taker_gets]);
    obj[jss::seq] = offer->getFieldU32 (sfSequence);
    obj[jss::flags] = offer->getFieldU32 (sfFlags);
    obj[jss::quality] = dirRate.getText ();
    if (offer->isFieldPresent(sfExpiration))
        obj[jss::expiration] = offer->getFieldU32(sfExpiration);
};
Json::Value doAccountOffers (RPC::Context& context)
{
    auto const& params (context.params);
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);
    if (! ledger)
        return result;
    std::string strIdent (params[jss::account].asString ());
    AccountID accountID;
    if (auto jv = RPC::accountFromString (accountID, strIdent))
    {
        for (auto it = jv.begin (); it != jv.end (); ++it)
            result[it.memberName ()] = (*it);
        return result;
    }
    result[jss::account] = context.app.accountIDCache().toBase58 (accountID);
    if (! ledger->exists(keylet::account (accountID)))
        return rpcError (rpcACT_NOT_FOUND);
    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountOffers, context))
        return *err;
    Json::Value& jsonOffers (result[jss::offers] = Json::arrayValue);
    std::vector <std::shared_ptr<SLE const>> offers;
    unsigned int reserve (limit);
    uint256 startAfter;
    std::uint64_t startHint;
    if (params.isMember(jss::marker))
    {
        Json::Value const& marker (params[jss::marker]);
        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");
        startAfter.SetHex (marker.asString ());
        auto const sleOffer = ledger->read({ltOFFER, startAfter});
        if (! sleOffer || accountID != sleOffer->getAccountID (sfAccount))
        {
            return rpcError (rpcINVALID_PARAMS);
        }
        startHint = sleOffer->getFieldU64(sfOwnerNode);
        appendOfferJson(sleOffer, jsonOffers);
        offers.reserve (reserve);
    }
    else
    {
        startHint = 0;
        offers.reserve (++reserve);
    }
    if (! forEachItemAfter(*ledger, accountID,
            startAfter, startHint, reserve,
        [&offers](std::shared_ptr<SLE const> const& offer)
        {
            if (offer->getType () == ltOFFER)
            {
                offers.emplace_back (offer);
                return true;
            }
            return false;
        }))
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    if (offers.size () == reserve)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string (offers.back ()->key ());
        offers.pop_back ();
    }
    for (auto const& offer : offers)
        appendOfferJson(offer, jsonOffers);
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}
} 
