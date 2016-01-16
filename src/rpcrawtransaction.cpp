// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "base58.h"
#include "rpcserver.h"
#include "txdb.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "keystore.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif

using namespace std;
using namespace boost;
using namespace boost::assign;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(Pair("asm", scriptPubKey.ToString()));

    if (fIncludeHex)
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out.push_back(Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(Pair("reqSigs", nRequired));
    out.push_back(Pair("type", GetTxnOutputType(type)));

    UniValue a(UniValue::VARR);
    BOOST_FOREACH(const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.push_back(Pair("addresses", a));
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    entry.push_back(Pair("txid", tx.GetHash().GetHex()));
    entry.push_back(Pair("version", tx.nVersion));
    entry.push_back(Pair("time", (int64_t)tx.nTime));
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    if (tx.nVersion > CTransaction::LEGACY_VERSION_1)
    {
      entry.push_back(Pair("clam-speech", tx.strCLAMSpeech));
    }

    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else
        {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            UniValue o(UniValue::VOBJ);
            o.push_back(Pair("asm", txin.scriptSig.ToString()));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));
        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, false);
        out.push_back(Pair("scriptPubKey", o));
        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    if (hashBlock != 0)
    {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
        {
            CBlockIndex* pindex = (*mi).second;
            if (pindex->IsInMainChain())
            {
                entry.push_back(Pair("confirmations", 1 + nBestHeight - pindex->nHeight));
                entry.push_back(Pair("time", (int64_t)pindex->nTime));
                entry.push_back(Pair("blocktime", (int64_t)pindex->nTime));
            }
            else
                entry.push_back(Pair("confirmations", 0));
        }
    }
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction <txid> [verbose=0]\n"
            "If verbose=0, returns a string that is\n"
            "serialized, hex-encoded data for <txid>.\n"
            "If verbose is non-zero, returns an Object\n"
            "with information about <txid>.");

    uint256 hash;
    hash.SetHex(params[0].get_str());

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    string strHex = HexStr(ssTx.begin(), ssTx.end());

    if (!fVerbose)
        return strHex;

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", strHex));
    TxToJSON(tx, hashBlock, result);
    return result;
}

#ifdef ENABLE_WALLET
UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw runtime_error(
            "listunspent [minconf=1] [maxconf=9999999]  [\"address\",...] [mature=1]\n"
            "Returns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filtered to only include txouts paid to specified addresses.\n"
            "Empty address list means do not filter.\n"
            "If mature is non-zero, only mature outputs will be listed.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}");

    RPCTypeCheck(params, list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VARR));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2)
    {
        UniValue inputs = params[2].get_array();

        for (unsigned int idx = 0; idx < inputs.size(); idx++)
        //BOOST_FOREACH(UniValue& input, inputs)
        {
            CBitcoinAddress address(inputs[idx].get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Clam address: ")+inputs[idx].get_str());
           setAddress.insert(address);
        }
    }

    bool fMature = true;
    if (params.size() > 3)
        fMature = (params[3].get_int() != 0);

    UniValue results(UniValue::VARR);
    vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    pwalletMain->AvailableCoins(vecOutputs, false, NULL, fMature);
    BOOST_FOREACH(const COutput& out, vecOutputs)
    {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if(setAddress.size())
        {
            CTxDestination address;
            if(!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        int64_t nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));
            if (pwalletMain->mapAddressBook.count(address))
                entry.push_back(Pair("account", pwalletMain->mapAddressBook[address]));
        }
        entry.push_back(Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
        if (pk.IsPayToScriptHash())
        {
            CTxDestination address;
            if (ExtractDestination(pk, address))
            {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }
        entry.push_back(Pair("amount",ValueFromAmount(nValue)));
        entry.push_back(Pair("confirmations",out.nDepth));
        results.push_back(entry);
    }

    return results;
}
#endif

UniValue createrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() < 2 || params.size() > 3))
        throw runtime_error(
            "createrawtransaction [{\"txid\":txid,\"vout\":n},...] {address:amount,...} [tx-comment]\n"
            "Create a transaction spending given inputs\n"
            "(array of objects containing transaction id and output number),\n"
            "sending to given address(es).\n"
            "Each <amount> is either an amount to send or {\"count\":c,\"amount\":a,\"locktime\":t}\n"
            "which adds <c> outputs each sending <a> CLAMs with a locktime of <t>.\n"
            "The count <c> and locktime <t> are both optional.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.");

    RPCTypeCheck(params, list_of(UniValue::VARR)(UniValue::VOBJ));

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    CTransaction rawTx;

    // set clamSpeech when creating a raw transaction
    if (params.size() == 3)
        rawTx.strCLAMSpeech = params[2].get_str();
    else if (mapArgs["-clamspeech"] != "off")
        rawTx.strCLAMSpeech = GetDefaultClamSpeech();

    if (rawTx.strCLAMSpeech.length() > MAX_TX_COMMENT_LEN)
        rawTx.strCLAMSpeech.resize(MAX_TX_COMMENT_LEN);

    for (unsigned int idx = 0; idx < inputs.size(); idx++)
    {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        const UniValue& txid_v = find_value(o, "txid");
        if (txid_v.isNull())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing txid key");
        string txid = txid_v.get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        CTxIn in(COutPoint(uint256(txid), nOutput));
        rawTx.vin.push_back(in);
    }

    // set<CBitcoinAddress> setAddress;
    vector<string> addrList = sendTo.getKeys();
    vector<UniValue> valueList= sendTo.getValues();
    vector<UniValue>::iterator valueList_iterator = valueList.begin();
    BOOST_FOREACH(const string& name_, addrList)
    {
        const UniValue& value_ = *valueList_iterator++;

        CBitcoinAddress address(name_);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Clam address: ")+name_);

        // if (setAddress.count(address))
        //     throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+name_);
        // setAddress.insert(address);

        CScript scriptPubKey;
        scriptPubKey.SetDestination(address.Get());

        // we usually want the 2nd parameter to be {"addr1":ammount1,"addr2":ammount2,...}
        // in addition to numerical amounts we now also accept values like:
        //   '{"count":2,"amount":1.3,"locktime":333333}' meaning 'make 2 outputs worth 1.3 CLAM each, locked until block 333333'
        // "count" is a non-negative integer; 0 works and simply makes no new outputs
        // "locktime" is a non-negative integer; values less than LOCKTIME_THRESHOLD (500,000,000) are block heights; others are epoch timestamps
        if (value_.isObject()) {
            const UniValue& o = value_.get_obj();
            const UniValue& count_v = find_value(o, "count");
            const UniValue& locktime_v = find_value(o, "locktime");
            int64_t count, locktime;

            if (count_v.isNull())
                count = 1;
            else if (count_v.isNum())
                count = count_v.get_int64();
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, count must be integer");

            if (count < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, count must not be negative");

            if (!locktime_v.isNull()) {
                if (locktime_v.isNum())
                    locktime = locktime_v.get_int64();
                else
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime must be integer");

                if (locktime < 0)
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime must not be negative");

                scriptPubKey.SetLockTime(locktime);
            }

            CTxOut out(AmountFromValue(find_value(o, "amount")), scriptPubKey);

            while (count--)
                rawTx.vout.push_back(out);
        } else {
            CTxOut out(AmountFromValue(value_), scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    return HexStr(ss.begin(), ss.end());
}

UniValue decoderawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction <hex string>\n"
            "Return a JSON object representing the serialized, hex-encoded transaction.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    vector<unsigned char> txData(ParseHex(params[0].get_str()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, 0, result);

    return result;
}

UniValue decodescript(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript <hex string>\n"
            "Decode a hex-encoded script.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", CBitcoinAddress(script.GetID()).ToString()));
    return r;
}

UniValue signrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction <hex string> [{\"txid\":txid,\"vout\":n,\"scriptPubKey\":hex,\"redeemScript\":hex},...] [<privatekey1>,...] [sighashtype=\"ALL\"]\n"
            "Sign inputs for raw transaction (serialized, hex-encoded).\n"
            "Second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the blockchain.\n"
            "Third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that can also be used to sign the transaction.\n"
            "Fourth optional argument is a string that is one of six values; ALL, NONE, SINGLE or\n"
            "ALL|ANYONECANPAY, NONE|ANYONECANPAY, SINGLE|ANYONECANPAY.\n"
            "Returns json object with keys:\n"
            "  hex : raw transaction with signature(s) (hex-encoded string)\n"
            "  complete : 1 if transaction has a complete set of signature (0 if not)"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase()
#endif
            );

    RPCTypeCheck(params, list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR), true);

    vector<unsigned char> txData(ParseHex(params[0].get_str()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CTransaction> txVariants;
    while (!ssData.empty())
    {
        try {
            CTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (std::exception &e) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CTransaction mergedTx(txVariants[0]);
    bool fComplete = true;

    // Fetch previous transactions (inputs):
    map<COutPoint, CScript> mapPrevOut;
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTransaction tempTx;
        MapPrevTx mapPrevTx;
        CTxDB txdb("r");
        map<uint256, CTxIndex> unused;
        bool fInvalid;

        // FetchInputs aborts on failure, so we go one at a time.
        tempTx.vin.push_back(mergedTx.vin[i]);
        tempTx.FetchInputs(txdb, unused, false, false, mapPrevTx, fInvalid);

        // Copy results into mapPrevOut:
        BOOST_FOREACH(const CTxIn& txin, tempTx.vin)
        {
            const uint256& prevHash = txin.prevout.hash;
            if (mapPrevTx.count(prevHash) && mapPrevTx[prevHash].second.vout.size()>txin.prevout.n)
                mapPrevOut[txin.prevout] = mapPrevTx[prevHash].second.vout[txin.prevout.n].scriptPubKey;
        }
    }

    bool fGivenKeys = false;
#ifdef ENABLE_WALLET
    CMergedKeyStore tempKeystore(pwalletMain);
#else
    CBasicKeyStore tempKeystore;
#endif
    if (params.size() > 2 && !params[2].isNull())
    {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();

        for (unsigned int idx = 0; idx < keys.size(); idx++) 
        {
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(keys[idx].get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();

            // 5TY1TqhFLax7w7Hoa6YtrejzoAuWTf5Tf1RKSvVED9tadmUMYjX and Lg7s3TveHSrHbpmAZ5Vmt9an6nim2f4gekrLRR5bJeEfT2fXkRoT are the zero privkey
            // they pass the above check, but aren't valid privkeys
            if (!key.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            }
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull())
    {
        UniValue prevTxs = params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) 
        {   const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR));

            string txidHex = find_value(prevOut, "txid").get_str();
            if (!IsHex(txidHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "txid must be hexadecimal");
            uint256 txid;
            txid.SetHex(txidHex);

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            string pkHex = find_value(prevOut, "scriptPubKey").get_str();
            if (!IsHex(pkHex))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "scriptPubKey must be hexadecimal");
            vector<unsigned char> pkData(ParseHex(pkHex));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            COutPoint outpoint(txid, nOut);
            if (mapPrevOut.count(outpoint))
            {
                // Complain if scriptPubKey doesn't match
                if (mapPrevOut[outpoint] != scriptPubKey)
                {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + mapPrevOut[outpoint].ToString() + "\nvs:\n"+
                        scriptPubKey.ToString();
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
            }
            else
                mapPrevOut[outpoint] = scriptPubKey;

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash())
            {
                RPCTypeCheckObj(prevOut, map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR)("redeemScript",UniValue::VSTR));
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull())
                {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && params[3].isNull())
    {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of
            (string("ALL"), int(SIGHASH_ALL))
            (string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
            (string("NONE"), int(SIGHASH_NONE))
            (string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
            (string("SINGLE"), int(SIGHASH_SINGLE))
            (string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY))
            ;
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn& txin = mergedTx.vin[i];
        if (mapPrevOut.count(txin.prevout) == 0)
        {
            fComplete = false;
            continue;
        }
        const CScript& prevPubKey = mapPrevOut[txin.prevout];

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(tempKeystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CTransaction& txv, txVariants)
        {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, prevPubKey, mergedTx, i, STANDARD_SCRIPT_VERIFY_FLAGS, 0, 0, 0))
            fComplete = false;
    }

    UniValue result(UniValue::VOBJ);
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mergedTx;
    result.push_back(Pair("hex", HexStr(ssTx.begin(), ssTx.end())));
    result.push_back(Pair("complete", fComplete));

    return result;
}

UniValue sendrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "sendrawtransaction <hex string>\n"
            "Submits raw transaction (serialized, hex-encoded) to local node and network.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    // parse hex string from parameter
    vector<unsigned char> txData(ParseHex(params[0].get_str()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;

    // deserialize binary data stream
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    uint256 hashTx = tx.GetHash();

    // See if the transaction is already in a block
    // or in the memory pool:
    CTransaction existingTx;
    uint256 hashBlock = 0;
    if (GetTransaction(hashTx, existingTx, hashBlock))
    {
        if (hashBlock != 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("transaction already in block ")+hashBlock.GetHex());
        // Not in block, but already in the memory pool; will drop
        // through to re-relay it.
    }
    else
    {
        // push to local node
        if (!AcceptToMemoryPool(mempool, tx, true, NULL))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");
    }
    RelayTransaction(tx, hashTx);

    return hashTx.GetHex();
}

void checkknowndatatype(std::string strType)
{
  if (!(strType == "qtx" || strType == "qblockheader" || strType == "qblockdelta" || strType == "qblockdeltah" || strType == "qctree"))
    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "unknown data type");
}

UniValue adddataaux(const UniValue& params, bool relay)
{
    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash; // in practice 160 bit hashes are used, making it 256 using 94 leading 0s
    hash.SetHex(params[1].get_str());

    // parse hex string from parameter
    vector<unsigned char> msgData(ParseHex(params[2].get_str()));

    if (!CTxDB("r").ContainsData(hash,strType)) {
      vector<unsigned char> datatxData(ParseHex("0200000000000000000000000000")); // dummy tx with no inputs and no outputs, storing data in clamspeech
      unsigned int l = msgData.size();
      if (l < 253) {
	datatxData.push_back((unsigned char) l);
      } else if (l < 65536) {
	datatxData.push_back((unsigned char) 253);
	datatxData.push_back((unsigned char) (l%256));
	datatxData.push_back((unsigned char) (l>>8));
      } else if (l < 1073741824) {
	datatxData.push_back((unsigned char) 254);
	datatxData.push_back((unsigned char) (l%256));
	datatxData.push_back((unsigned char) ((l>>8)%256));
	datatxData.push_back((unsigned char) ((l>>16)%256));
	datatxData.push_back((unsigned char) (l>>24));
      } else { // too big
	throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "data too big; 1MB limit breached");
      }
      unsigned int i;
      for (i = 0; i < l; ++i) {
	datatxData.push_back(msgData[i]);
      }
      CDataStream dtxData(datatxData, SER_NETWORK, PROTOCOL_VERSION);
      CTransaction datatx;
      dtxData >> datatx;
      CTxDB().WriteData(hash,strType,datatx);
      if (strType == "qtx") {
	fprintf(stderr,"\nQTX:%s\n",hash.GetHex().c_str());
      }
      if (strType == "qblockheader") {
	fprintf(stderr,"\nQHEADER:%s\n",hash.GetHex().c_str());
      }
    }
    if (relay)
      {
	CDataStream ssData(msgData, SER_NETWORK, PROTOCOL_VERSION);
	RelayMessage(strType,msgData,hash,ssData);
      }

    return hash.GetHex();
}

UniValue adddatafromfileaux(const UniValue& params, bool relay)
{
    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash; // in practice 160 bit hashes are used, making it 256 using 94 leading 0s
    hash.SetHex(params[1].get_str());

    // get data from file
    vector<unsigned char> msgData(ParseHex("")); // obviously I'm not sure how to make an empty vector
    std::FILE *fp = std::fopen(params[2].get_str().c_str(), "rb");
    if (fp)
      {
	std::string contents;
	std::fseek(fp, 0, SEEK_END);
	contents.resize(std::ftell(fp));
	std::rewind(fp);
	std::fread(&contents[0], 1, contents.size(), fp);
	std::fclose(fp);
	const char* contentsc = contents.c_str();
	for (unsigned int i = 0; i < contents.size(); ++i)
	  {
	    msgData.push_back((unsigned char) contentsc[i]);
	  }
      } else
      {
	throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "could not find/open file");
      }
    if (!CTxDB("r").ContainsData(hash,strType)) {
      vector<unsigned char> datatxData(ParseHex("0200000000000000000000000000")); // dummy tx with no inputs and no outputs, storing data in clamspeech
      unsigned int l = msgData.size();
      if (l < 253) {
	datatxData.push_back((unsigned char) l);
      } else if (l < 65536) {
	datatxData.push_back((unsigned char) 253);
	datatxData.push_back((unsigned char) (l%256));
	datatxData.push_back((unsigned char) (l>>8));
      } else if (l < 1073741824) {
	datatxData.push_back((unsigned char) 254);
	datatxData.push_back((unsigned char) (l%256));
	datatxData.push_back((unsigned char) ((l>>8)%256));
	datatxData.push_back((unsigned char) ((l>>16)%256));
	datatxData.push_back((unsigned char) (l>>24));
      } else { // too big
	throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "data too big; 1MB limit breached");
      }
      unsigned int i;
      for (i = 0; i < l; ++i) {
	datatxData.push_back(msgData[i]);
      }
      CDataStream dtxData(datatxData, SER_NETWORK, PROTOCOL_VERSION);
      CTransaction datatx;
      dtxData >> datatx;
      CTxDB().WriteData(hash,strType,datatx);
      if (strType == "qtx") {
	fprintf(stderr,"\nQTX:%s\n",hash.GetHex().c_str());
      }
      if (strType == "qblockheader") {
	fprintf(stderr,"\nQHEADER:%s\n",hash.GetHex().c_str());
      }
    }
    if (relay) {
      CDataStream ssData(msgData, SER_NETWORK, PROTOCOL_VERSION);
      RelayMessage(strType,msgData,hash,ssData);
    }

    return hash.GetHex();
}

UniValue adddata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 3)
        throw runtime_error(
            "adddata <type> <hash160hex> <hex string>\n"
            "Add data to local database and send it as inv to peers.");

    return adddataaux(params,true);
}

UniValue adddatafromfile(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 3)
        throw runtime_error(
            "adddatafile <type> <hash160hex> <filename>\n"
            "Add data from a file to local database and send it as inv to peers.");

    return adddatafromfileaux(params,true);
}

UniValue savedata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 3)
        throw runtime_error(
            "savedata <type> <hash> <hex string>\n"
            "Add data to the local database without sending inv to peers.");

    return adddataaux(params,false);
}

UniValue savedatafromfile(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 3)
        throw runtime_error(
            "savedatafromfile <type> <hash> <filename>\n"
            "Add data from a file to the local database without sending inv to peers.");

    return adddatafromfileaux(params,false);
}

UniValue relaydata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "relaydata <type> <hash>\n"
            "Relays the data (as inventory) to peers, assuming it's in the local db.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash;
    hash.SetHex(params[1].get_str());

    CTransaction datatx;

    if (!CTxDB("r").ReadData(hash, strType, datatx)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No data");
    }

    vector<unsigned char> msgData(ParseHex("")); // obviously I'm not sure how to make an empty vector
    string contents = datatx.strCLAMSpeech;
    const char* contentsc = contents.c_str();
    for (unsigned int i = 0; i < contents.size(); ++i)
      {
	msgData.push_back((unsigned char) contentsc[i]);
      }

    CDataStream ssData(msgData, SER_NETWORK, PROTOCOL_VERSION);
    RelayMessage(strType,msgData,hash,ssData);

    return hash.GetHex();
}

UniValue removedata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "removedata <type> <hash>\n"
            "Remove data from the local database");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash; // in practice 160 bit hashes are used, making it 256 using 94 leading 0s
    hash.SetHex(params[1].get_str());

    if (CTxDB().EraseData(hash,strType)) {
      return "erased";
    } else {
      return "not erased";
    }
}

UniValue loaddata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "loaddata <type> <hash>\n"
            "Gets data from local database.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash;
    hash.SetHex(params[1].get_str());

    CTransaction datatx;

    if (!CTxDB("r").ReadData(hash, strType, datatx)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No data");
    }

    string strHex = HexStr(datatx.strCLAMSpeech);

    return strHex;
}

UniValue getdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "getdata <type> <hash>\n"
            "Gets data from local database, requesting it from peers if we don't have it.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash;
    hash.SetHex(params[1].get_str());

    CTxDB txdb = CTxDB();

    if (txdb.ContainsData(hash, strType)) { // already have it
      return "already have";
    } else {
      if (!txdb.ContainsDataWhitelist(hash, strType)) // if not already whitelisted, whitelist it so main will request it
	txdb.AddDataWhitelist(hash,strType);
      RelayGetData(strType, hash);
      return "requested";
    }
}


UniValue whitelistdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "whitelistdata <type> <hash>\n"
            "If data is not already in the local database, whitelist it so that it will be requested when seen in a peer's inv.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash;
    hash.SetHex(params[1].get_str());

    CTxDB txdb = CTxDB();
    if (!txdb.ContainsData(hash, strType)) {
      if (!txdb.ContainsDataWhitelist(hash, strType)) {
	txdb.AddDataWhitelist(hash,strType);
	return "whitelisted";
      } else {
	return "already whitelisted";
      }
    } else {
      return "already have";
    }
}

UniValue blacklistdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
            "blacklistdata <type> <hash>\n"
            "Blacklist data so it will not be requested. This is used if data was deleted and we don't want it to appear to be new. If the data is already in the database, it is erased.");

    RPCTypeCheck(params, list_of(UniValue::VSTR));

    std::string strType = params[0].get_str();
    checkknowndatatype(strType);
    uint256 hash;
    hash.SetHex(params[1].get_str());

    CTxDB txdb = CTxDB();
    txdb.AddDataBlacklist(hash,strType);
    txdb.EraseData(hash,strType);
    return "blacklisted";
}

