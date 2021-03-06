
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/STAmount.h>
namespace ripple {
class OwnerInfo_test : public beast::unit_test::suite
{
    void
    testBadInput ()
    {
        testcase ("Bad input to owner_info");
        using namespace test::jtx;
        Env env {*this};
        auto const alice = Account {"alice"};
        env.fund (XRP(10000), alice);
        env.close ();
        { 
            auto const result =
                env.rpc ("json", "owner_info", "{}") [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Missing field 'account'.");
        }
        { 
            Json::Value params;
            params[jss::account] = "";
            auto const result = env.rpc ("json", "owner_info",
                to_string(params)) [jss::result];
            if (BEAST_EXPECT (
                result.isMember(jss::accepted) &&
                result.isMember(jss::current)))
            {
                BEAST_EXPECT (result[jss::accepted][jss::error] == "badSeed");
                BEAST_EXPECT (result[jss::accepted][jss::error_message] ==
                    "Disallowed seed.");
                BEAST_EXPECT (result[jss::current][jss::error] == "badSeed");
                BEAST_EXPECT (result[jss::current][jss::error_message] ==
                    "Disallowed seed.");
            }
        }
        { 
            Json::Value params;
            params[jss::account] = Account{"bob"}.human();
            auto const result = env.rpc ("json", "owner_info",
                to_string(params)) [jss::result];
            BEAST_EXPECT (result[jss::accepted] == Json::objectValue);
            BEAST_EXPECT (result[jss::current] == Json::objectValue);
            BEAST_EXPECT (result[jss::status] == "success");
        }
    }
    void
    testBasic ()
    {
        testcase ("Basic request for owner_info");
        using namespace test::jtx;
        Env env {*this};
        auto const alice = Account {"alice"};
        auto const gw = Account {"gateway"};
        env.fund (XRP(10000), alice, gw);
        auto const USD = gw["USD"];
        auto const CNY = gw["CNY"];
        env(trust(alice, USD(1000)));
        env(trust(alice, CNY(1000)));
        env(offer(alice, USD(1), XRP(1000)));
        env.close();
        env(pay(gw, alice, USD(50)));
        env(pay(gw, alice, CNY(50)));
        env(offer(alice, CNY(2), XRP(1000)));
        Json::Value params;
        params[jss::account] = alice.human();
        auto const result = env.rpc ("json", "owner_info",
            to_string(params)) [jss::result];
        if (! BEAST_EXPECT (
                result.isMember(jss::accepted) &&
                result.isMember(jss::current)))
        {
            return;
        }
        if (! BEAST_EXPECT (result[jss::accepted].isMember(jss::ripple_lines)))
            return;
        auto lines = result[jss::accepted][jss::ripple_lines];
        if (! BEAST_EXPECT (lines.isArray() && lines.size() == 2))
            return;
        BEAST_EXPECT (
            lines[0u][sfBalance.fieldName] ==
            (STAmount{Issue{to_currency("CNY"), noAccount()}, 0}
                 .value().getJson(JsonOptions::none)));
        BEAST_EXPECT (
            lines[0u][sfHighLimit.fieldName] ==
            alice["CNY"](1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[0u][sfLowLimit.fieldName] ==
            gw["CNY"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[1u][sfBalance.fieldName] ==
            (STAmount{Issue{to_currency("USD"), noAccount()}, 0}
                .value().getJson(JsonOptions::none)));
        BEAST_EXPECT (
            lines[1u][sfHighLimit.fieldName] ==
            alice["USD"](1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[1u][sfLowLimit.fieldName] ==
            USD(0).value().getJson(JsonOptions::none));
        if (! BEAST_EXPECT (result[jss::accepted].isMember(jss::offers)))
            return;
        auto offers = result[jss::accepted][jss::offers];
        if (! BEAST_EXPECT (offers.isArray() && offers.size() == 1))
            return;
        BEAST_EXPECT (
            offers[0u][jss::Account] == alice.human());
        BEAST_EXPECT (
            offers[0u][sfTakerGets.fieldName] ==
            XRP(1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            offers[0u][sfTakerPays.fieldName] ==
            USD(1).value().getJson(JsonOptions::none));
        if (! BEAST_EXPECT (result[jss::current].isMember(jss::ripple_lines)))
            return;
        lines = result[jss::current][jss::ripple_lines];
        if (! BEAST_EXPECT (lines.isArray() && lines.size() == 2))
            return;
        BEAST_EXPECT (
            lines[0u][sfBalance.fieldName] ==
            (STAmount{Issue{to_currency("CNY"), noAccount()}, -50}
                 .value().getJson(JsonOptions::none)));
        BEAST_EXPECT (
            lines[0u][sfHighLimit.fieldName] ==
            alice["CNY"](1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[0u][sfLowLimit.fieldName] ==
            gw["CNY"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[1u][sfBalance.fieldName] ==
            (STAmount{Issue{to_currency("USD"), noAccount()}, -50}
                .value().getJson(JsonOptions::none)));
        BEAST_EXPECT (
            lines[1u][sfHighLimit.fieldName] ==
            alice["USD"](1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            lines[1u][sfLowLimit.fieldName] ==
            gw["USD"](0).value().getJson(JsonOptions::none));
        if (! BEAST_EXPECT (result[jss::current].isMember(jss::offers)))
            return;
        offers = result[jss::current][jss::offers];
        if (! BEAST_EXPECT (offers.isArray() && offers.size() == 2))
            return;
        BEAST_EXPECT (
            offers[1u] == result[jss::accepted][jss::offers][0u]);
        BEAST_EXPECT (
            offers[0u][jss::Account] == alice.human());
        BEAST_EXPECT (
            offers[0u][sfTakerGets.fieldName] ==
            XRP(1000).value().getJson(JsonOptions::none));
        BEAST_EXPECT (
            offers[0u][sfTakerPays.fieldName] ==
            CNY(2).value().getJson(JsonOptions::none));
    }
public:
    void run () override
    {
        testBadInput ();
        testBasic ();
    }
};
BEAST_DEFINE_TESTSUITE(OwnerInfo,app,ripple);
} 
