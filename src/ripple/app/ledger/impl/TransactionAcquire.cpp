
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/ledger/ConsensusTransSetSF.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/basics/make_lock.h>
#include <memory>
namespace ripple {
using namespace std::chrono_literals;
auto constexpr TX_ACQUIRE_TIMEOUT = 250ms;
enum
{
    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};
TransactionAcquire::TransactionAcquire (Application& app, uint256 const& hash, clock_type& clock)
    : PeerSet (app, hash, TX_ACQUIRE_TIMEOUT, clock,
        app.journal("TransactionAcquire"))
    , mHaveRoot (false)
    , j_(app.journal("TransactionAcquire"))
{
    mMap = std::make_shared<SHAMap> (SHAMapType::TRANSACTION, hash,
        app_.family(), SHAMap::version{1});
    mMap->setUnbacked ();
}
void TransactionAcquire::execute ()
{
    app_.getJobQueue ().addJob (
        jtTXN_DATA, "TransactionAcquire",
        [ptr = shared_from_this()](Job&)
        {
            ptr->invokeOnTimer ();
        });
}
void TransactionAcquire::done ()
{
    if (mFailed)
    {
        JLOG (j_.warn()) << "Failed to acquire TX set " << mHash;
    }
    else
    {
        JLOG (j_.debug()) << "Acquired TX set " << mHash;
        mMap->setImmutable ();
        uint256 const& hash (mHash);
        std::shared_ptr <SHAMap> const& map (mMap);
        auto const pap = &app_;
        app_.getJobQueue().addJob (jtTXN_DATA, "completeAcquire",
            [pap, hash, map](Job&)
            {
                pap->getInboundTransactions().giveSet (
                    hash, map, true);
            });
    }
}
void TransactionAcquire::onTimer (bool progress, ScopedLockType& psl)
{
    bool aggressive = false;
    if (getTimeouts () >= NORM_TIMEOUTS)
    {
        aggressive = true;
        if (getTimeouts () > MAX_TIMEOUTS)
        {
            mFailed = true;
            done ();
            return;
        }
    }
    if (aggressive)
        trigger (nullptr);
    addPeers (1);
}
std::weak_ptr<PeerSet> TransactionAcquire::pmDowncast ()
{
    return std::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}
void TransactionAcquire::trigger (std::shared_ptr<Peer> const& peer)
{
    if (mComplete)
    {
        JLOG (j_.info()) << "trigger after complete";
        return;
    }
    if (mFailed)
    {
        JLOG (j_.info()) << "trigger after fail";
        return;
    }
    if (!mHaveRoot)
    {
        JLOG (j_.trace()) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer") << " no root";
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash (mHash.begin (), mHash.size ());
        tmGL.set_itype (protocol::liTS_CANDIDATE);
        tmGL.set_querydepth (3); 
        if (getTimeouts () != 0)
            tmGL.set_querytype (protocol::qtINDIRECT);
        * (tmGL.add_nodeids ()) = SHAMapNodeID ().getRawString ();
        sendRequest (tmGL, peer);
    }
    else if (!mMap->isValid ())
    {
        mFailed = true;
        done ();
    }
    else
    {
        ConsensusTransSetSF sf (app_, app_.getTempNodeCache ());
        auto nodes = mMap->getMissingNodes (256, &sf);
        if (nodes.empty ())
        {
            if (mMap->isValid ())
                mComplete = true;
            else
                mFailed = true;
            done ();
            return;
        }
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash (mHash.begin (), mHash.size ());
        tmGL.set_itype (protocol::liTS_CANDIDATE);
        if (getTimeouts () != 0)
            tmGL.set_querytype (protocol::qtINDIRECT);
        for (auto const& node : nodes)
        {
            *tmGL.add_nodeids () = node.first.getRawString ();
        }
        sendRequest (tmGL, peer);
    }
}
SHAMapAddNode TransactionAcquire::takeNodes (const std::list<SHAMapNodeID>& nodeIDs,
        const std::list< Blob >& data, std::shared_ptr<Peer> const& peer)
{
    ScopedLockType sl (mLock);
    if (mComplete)
    {
        JLOG (j_.trace()) << "TX set complete";
        return SHAMapAddNode ();
    }
    if (mFailed)
    {
        JLOG (j_.trace()) << "TX set failed";
        return SHAMapAddNode ();
    }
    try
    {
        if (nodeIDs.empty ())
            return SHAMapAddNode::invalid ();
        std::list<SHAMapNodeID>::const_iterator nodeIDit = nodeIDs.begin ();
        std::list< Blob >::const_iterator nodeDatait = data.begin ();
        ConsensusTransSetSF sf (app_, app_.getTempNodeCache ());
        while (nodeIDit != nodeIDs.end ())
        {
            if (nodeIDit->isRoot ())
            {
                if (mHaveRoot)
                    JLOG (j_.debug()) << "Got root TXS node, already have it";
                else if (!mMap->addRootNode (SHAMapHash{getHash ()},
                                             makeSlice(*nodeDatait), snfWIRE, nullptr).isGood())
                {
                    JLOG (j_.warn()) << "TX acquire got bad root node";
                }
                else
                    mHaveRoot = true;
            }
            else if (!mMap->addKnownNode (*nodeIDit, makeSlice(*nodeDatait), &sf).isGood())
            {
                JLOG (j_.warn()) << "TX acquire got bad non-root node";
                return SHAMapAddNode::invalid ();
            }
            ++nodeIDit;
            ++nodeDatait;
        }
        trigger (peer);
        progress ();
        return SHAMapAddNode::useful ();
    }
    catch (std::exception const&)
    {
        JLOG (j_.error()) << "Peer sends us junky transaction node data";
        return SHAMapAddNode::invalid ();
    }
}
void TransactionAcquire::addPeers (int numPeers)
{
    app_.overlay().selectPeers (*this, numPeers, ScoreHasTxSet (getHash()));
}
void TransactionAcquire::init (int numPeers)
{
    ScopedLockType sl (mLock);
    addPeers (numPeers);
    setTimer ();
}
void TransactionAcquire::stillNeed ()
{
    ScopedLockType sl (mLock);
    if (mTimeouts > NORM_TIMEOUTS)
        mTimeouts = NORM_TIMEOUTS;
}
} 
