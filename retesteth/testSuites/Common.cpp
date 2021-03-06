#include "Common.h"
#include <retesteth/DataObject.h>
#include <retesteth/Options.h>
#include <retesteth/RPCSession.h>
using namespace std;
namespace test
{
DataObject getRemoteState(RPCSession& _session, string const& _trHash, bool _fullPost)
{
    DataObject remoteState;
    const int cmaxRows = 1000;
    string latestBlockNumber = toString(u256(_session.eth_blockNumber()));

    test::scheme_block latestBlock = _session.eth_getBlockByNumber(latestBlockNumber, true);
    remoteState["postHash"] = latestBlock.getData().at("stateRoot");
    if (!_trHash.empty())
        remoteState["logHash"] = _session.test_getLogHash(_trHash);
    remoteState["postState"] = "";
    remoteState["rawBlockData"] = latestBlock.getData();

    if (_fullPost)
    {
        int trIndex = latestBlock.getTransactionCount(); //1000;
        DataObject accountObj;
        Json::Value res = _session.debug_accountRangeAt(latestBlockNumber, trIndex, "0", cmaxRows);
        for (auto acc : res["addressMap"])
        {
            // Balance
            Json::Value ret = _session.eth_getBalance(acc.asString(), latestBlockNumber);
            accountObj[acc.asString()]["balance"] =
                dev::toCompactHexPrefixed(u256(ret.asString()), 1);  // fix odd strings

            // Code
            ret = _session.eth_getCode(acc.asString(), latestBlockNumber);
            accountObj[acc.asString()]["code"] = ret.asString();

            // Nonce
            ret = _session.eth_getTransactionCount(acc.asString(), latestBlockNumber);
            accountObj[acc.asString()]["nonce"] =
                dev::toCompactHexPrefixed(u256(ret.asString()), 1);

            // Storage
            DataObject storage(DataType::Object);
            DataObject debugStorageAt = test::convertJsonCPPtoData(_session.debug_storageRangeAt(
                latestBlockNumber, trIndex, acc.asString(), "0", cmaxRows));
            for (auto const& element : debugStorageAt["storage"].getSubObjects())
                storage[element.at("key").asString()] = element.at("value").asString();
            accountObj[acc.asString()]["storage"] = storage;
        }

        remoteState["postState"].clear();
        remoteState["postState"] = accountObj;
        if (Options::get().poststate)
            std::cout << accountObj.asJson() << std::endl;
    }
    return remoteState;
}
}
