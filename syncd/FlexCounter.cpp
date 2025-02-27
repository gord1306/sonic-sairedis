#include "FlexCounter.h"
#include "VidManager.h"

#include "meta/sai_serialize.h"

#include "swss/redisapi.h"
#include "swss/tokenize.h"

#include <inttypes.h>
#include <vector>

using namespace syncd;
using namespace std;

#define MUTEX std::unique_lock<std::mutex> _lock(m_mtx);
#define MUTEX_UNLOCK _lock.unlock();

FlexCounter::FlexCounter(
        _In_ const std::string& instanceId,
        _In_ std::shared_ptr<sairedis::SaiInterface> vendorSai,
        _In_ const std::string& dbCounters):
    m_readyToPoll(false),
    m_pollInterval(0),
    m_instanceId(instanceId),
    m_vendorSai(vendorSai),
    m_dbCounters(dbCounters)
{
    SWSS_LOG_ENTER();

    m_enable = false;
    m_isDiscarded = false;

    startFlexCounterThread();
}

FlexCounter::~FlexCounter(void)
{
    SWSS_LOG_ENTER();

    endFlexCounterThread();
}

FlexCounter::PortCounterIds::PortCounterIds(
        _In_ sai_object_id_t port,
        _In_ const std::vector<sai_port_stat_t> &portIds):
    portId(port), portCounterIds(portIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::SwitchCounterIds::SwitchCounterIds(
        _In_ sai_object_id_t oid,
        _In_ const std::vector<sai_switch_stat_t> &counterIds)
    : switchId(oid),
    switchCounterIds(counterIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::QueueCounterIds::QueueCounterIds(
        _In_ sai_object_id_t queue,
        _In_ const std::vector<sai_queue_stat_t> &queueIds):
    queueId(queue), queueCounterIds(queueIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::QueueAttrIds::QueueAttrIds(
        _In_ sai_object_id_t queue,
        _In_ const std::vector<sai_queue_attr_t> &queueIds):
    queueId(queue), queueAttrIds(queueIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::IngressPriorityGroupAttrIds::IngressPriorityGroupAttrIds(
        _In_ sai_object_id_t priorityGroup,
        _In_ const std::vector<sai_ingress_priority_group_attr_t> &priorityGroupIds):
    priorityGroupId(priorityGroup), priorityGroupAttrIds(priorityGroupIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::IngressPriorityGroupCounterIds::IngressPriorityGroupCounterIds(
        _In_ sai_object_id_t priorityGroup,
        _In_ const std::vector<sai_ingress_priority_group_stat_t> &priorityGroupIds):
    priorityGroupId(priorityGroup), priorityGroupCounterIds(priorityGroupIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::RifCounterIds::RifCounterIds(
        _In_ sai_object_id_t rif,
        _In_ const std::vector<sai_router_interface_stat_t> &rifIds):
    rifId(rif), rifCounterIds(rifIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::BufferPoolCounterIds::BufferPoolCounterIds(
        _In_ sai_object_id_t bufferPool,
        _In_ const std::vector<sai_buffer_pool_stat_t> &bufferPoolIds,
        _In_ sai_stats_mode_t statsMode):
    bufferPoolId(bufferPool), bufferPoolStatsMode(statsMode), bufferPoolCounterIds(bufferPoolIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::MACsecFlowCounterIds::MACsecFlowCounterIds(
        _In_ sai_object_id_t macsecFlow,
        _In_ const std::vector<sai_macsec_flow_stat_t> &macsecFlowIds):
        m_macsecFlowId(macsecFlow),
        m_macsecFlowCounterIds(macsecFlowIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::MACsecSACounterIds::MACsecSACounterIds(
        _In_ sai_object_id_t macsecSA,
        _In_ const std::vector<sai_macsec_sa_stat_t> &macsecSAIds):
        m_macsecSAId(macsecSA),
        m_macsecSACounterIds(macsecSAIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::MACsecSAAttrIds::MACsecSAAttrIds(
        _In_ sai_object_id_t macsecSA,
        _In_ const std::vector<sai_macsec_sa_attr_t> &macsecSAIds):
        m_macsecSAId(macsecSA),
        m_macsecSAAttrIds(macsecSAIds)
{
    SWSS_LOG_ENTER();
    // empty intentionally
}

FlexCounter::AclCounterAttrIds::AclCounterAttrIds(
        _In_ sai_object_id_t aclCounter,
        _In_ const std::vector<sai_acl_counter_attr_t> &aclCounterIds):
        m_aclCounterId(aclCounter),
        m_aclCounterAttrIds(aclCounterIds)
{
    SWSS_LOG_ENTER();
}

FlexCounter::TunnelCounterIds::TunnelCounterIds(
        _In_ sai_object_id_t tunnelRid,
        _In_ const std::vector<sai_tunnel_stat_t> &tunnelIds):
        m_tunnelId(tunnelRid),
        m_tunnelCounterIds(tunnelIds)
{
    SWSS_LOG_ENTER();
    // empty intentionally
}

FlexCounter::FlowCounterIds::FlowCounterIds(
        _In_ sai_object_id_t counter,
        _In_ const std::vector<sai_counter_stat_t> &flowCounters):
        counterId(counter),
        flowCounterIds(flowCounters)
{
    SWSS_LOG_ENTER();
    // empty intentionally
}

void FlexCounter::setPollInterval(
        _In_ uint32_t pollInterval)
{
    SWSS_LOG_ENTER();

    m_pollInterval = pollInterval;
}

void FlexCounter::setStatus(
        _In_ const std::string& status)
{
    SWSS_LOG_ENTER();

    if (status == "enable")
    {
        m_enable = true;
    }
    else if (status == "disable")
    {
        m_enable = false;
    }
    else
    {
        SWSS_LOG_WARN("Input value %s is not supported for Flex counter status, enter enable or disable", status.c_str());
    }
}

void FlexCounter::setStatsMode(
        _In_ const std::string& mode)
{
    SWSS_LOG_ENTER();

    if (mode == STATS_MODE_READ)
    {
        m_statsMode = SAI_STATS_MODE_READ;

        SWSS_LOG_DEBUG("Set STATS MODE %s for FC %s", mode.c_str(), m_instanceId.c_str());
    }
    else if (mode == STATS_MODE_READ_AND_CLEAR)
    {
        m_statsMode = SAI_STATS_MODE_READ_AND_CLEAR;

        SWSS_LOG_DEBUG("Set STATS MODE %s for FC %s", mode.c_str(), m_instanceId.c_str());
    }
    else
    {
        SWSS_LOG_WARN("Input value %s is not supported for Flex counter stats mode, enter STATS_MODE_READ or STATS_MODE_READ_AND_CLEAR", mode.c_str());
    }
}

void FlexCounter::addCollectCountersHandler(const std::string &key, const collect_counters_handler_t &handler)
{
    SWSS_LOG_ENTER();

    m_collectCountersHandlers.emplace(key, handler);
}

void FlexCounter::removeCollectCountersHandler(const std::string &key)
{
    SWSS_LOG_ENTER();

    m_collectCountersHandlers.erase(key);
}

/* The current implementation of 'setPortCounterList' and 'setQueueCounterList' are
 * not the same. Need to refactor these two functions to have the similar logic.
 * Either the full SAI attributes are queried once, or each of the needed counters
 * will be queried when they are set.
 */
void FlexCounter::setPortCounterList(
        _In_ sai_object_id_t portVid,
        _In_ sai_object_id_t portId,
        _In_ const std::vector<sai_port_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedPortCounters(portId);

    // Remove unsupported counters
    std::vector<sai_port_stat_t> supportedIds;

    for (auto &counter : counterIds)
    {
        if (isPortCounterSupported(counter))
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("Port %s does not has supported counters", sai_serialize_object_id(portId).c_str());
        return;
    }

    auto it = m_portCounterIdsMap.find(portVid);

    if (it != m_portCounterIdsMap.end())
    {
        it->second->portCounterIds = supportedIds;
        return;
    }

    auto portCounterIds = std::make_shared<PortCounterIds>(portId, supportedIds);

    m_portCounterIdsMap.emplace(portVid, portCounterIds);

    addCollectCountersHandler(PORT_COUNTER_ID_LIST, &FlexCounter::collectPortCounters);
}

void FlexCounter::setPortDebugCounterList(
        _In_ sai_object_id_t portVid,
        _In_ sai_object_id_t portId,
        _In_ const std::vector<sai_port_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    // Because debug counters can be added and removed over time, we currently
    // check that the provided list of counters is valid every time.
    auto supportedIds = saiCheckSupportedPortDebugCounters(portId, counterIds);

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("Port %s does not have supported debug counters", sai_serialize_object_id(portId).c_str());
        return;
    }

    auto it = m_portDebugCounterIdsMap.find(portVid);

    if (it != m_portDebugCounterIdsMap.end())
    {
        it->second->portCounterIds = supportedIds;
        return;
    }

    auto portDebugCounterIds = std::make_shared<PortCounterIds>(portId, supportedIds);

    m_portDebugCounterIdsMap.emplace(portVid, portDebugCounterIds);

    addCollectCountersHandler(PORT_DEBUG_COUNTER_ID_LIST, &FlexCounter::collectPortDebugCounters);
}

void FlexCounter::setQueueCounterList(
        _In_ sai_object_id_t queueVid,
        _In_ sai_object_id_t queueRid,
        _In_ const std::vector<sai_queue_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedQueueCounters(queueRid, counterIds);

    // Remove unsupported counters
    std::vector<sai_queue_stat_t> supportedIds;
    for (auto &counter : counterIds)
    {
        if (isQueueCounterSupported(counter))
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("%s: queue %s does not has supported counters", m_instanceId.c_str(), sai_serialize_object_id(queueRid).c_str());
        return;
    }

    // Check if queue is able to provide the statistic
    std::vector<uint64_t> queueStats(supportedIds.size());

    sai_status_t status = m_vendorSai->getStats(
            SAI_OBJECT_TYPE_QUEUE,
            queueRid,
            static_cast<uint32_t>(supportedIds.size()),
            (const sai_stat_id_t *)supportedIds.data(),
            queueStats.data());

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Queue RID %s can't provide the statistic",  sai_serialize_object_id(queueRid).c_str());
        return;
    }

    auto it = m_queueCounterIdsMap.find(queueVid);

    if (it != m_queueCounterIdsMap.end())
    {
        it->second->queueCounterIds = supportedIds;
        return;
    }

    auto queueCounterIds = std::make_shared<QueueCounterIds>(queueRid, supportedIds);

    m_queueCounterIdsMap.emplace(queueVid, queueCounterIds);

    addCollectCountersHandler(QUEUE_COUNTER_ID_LIST, &FlexCounter::collectQueueCounters);
}

void FlexCounter::setQueueAttrList(
        _In_ sai_object_id_t queueVid,
        _In_ sai_object_id_t queueRid,
        _In_ const std::vector<sai_queue_attr_t> &attrIds)
{
    SWSS_LOG_ENTER();

    auto it = m_queueAttrIdsMap.find(queueVid);

    if (it != m_queueAttrIdsMap.end())
    {
        it->second->queueAttrIds = attrIds;
        return;
    }

    auto queueAttrIds = std::make_shared<QueueAttrIds>(queueRid, attrIds);

    m_queueAttrIdsMap.emplace(queueVid, queueAttrIds);

    addCollectCountersHandler(QUEUE_ATTR_ID_LIST, &FlexCounter::collectQueueAttrs);
}

void FlexCounter::setPriorityGroupCounterList(
        _In_ sai_object_id_t priorityGroupVid,
        _In_ sai_object_id_t priorityGroupRid,
        _In_ const std::vector<sai_ingress_priority_group_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedPriorityGroupCounters(priorityGroupRid, counterIds);

    // Remove unsupported counters
    std::vector<sai_ingress_priority_group_stat_t> supportedIds;

    for (auto &counter : counterIds)
    {
        if (isPriorityGroupCounterSupported(counter))
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("%s: priority group %s does not have supported counters",
                m_instanceId.c_str(),
                sai_serialize_object_id(priorityGroupRid).c_str());
        return;
    }

    // Check if PG is able to provide the statistic
    std::vector<uint64_t> priorityGroupStats(supportedIds.size());

    sai_status_t status = m_vendorSai->getStats(
            SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
            priorityGroupRid,
            static_cast<uint32_t>(supportedIds.size()),
            (const sai_stat_id_t *)supportedIds.data(),
            priorityGroupStats.data());

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Priority group %s can't provide the statistic",  sai_serialize_object_id(priorityGroupRid).c_str());
        return;
    }

    auto it = m_priorityGroupCounterIdsMap.find(priorityGroupVid);

    if (it != m_priorityGroupCounterIdsMap.end())
    {
        it->second->priorityGroupCounterIds = supportedIds;
        return;
    }

    auto priorityGroupCounterIds = std::make_shared<IngressPriorityGroupCounterIds>(priorityGroupRid, supportedIds);

    m_priorityGroupCounterIdsMap.emplace(priorityGroupVid, priorityGroupCounterIds);

    addCollectCountersHandler(PG_COUNTER_ID_LIST, &FlexCounter::collectPriorityGroupCounters);
}

void FlexCounter::setSwitchDebugCounterList(
        _In_ sai_object_id_t switchVid,
        _In_ sai_object_id_t switchRid,
        _In_ const std::vector<sai_switch_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    // Because debug counters can be added and removed over time, we currently
    // check that the provided list of counters is valid every time.
    std::vector<sai_switch_stat_t> supportedIds = saiCheckSupportedSwitchDebugCounters(switchRid, counterIds);

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("Switch %s does not have supported debug counters", sai_serialize_object_id(switchRid).c_str());
        return;
    }

    auto it = m_switchDebugCounterIdsMap.find(switchVid);

    if (it != m_switchDebugCounterIdsMap.end())
    {
        it->second->switchCounterIds = supportedIds;
        return;
    }

    auto switchDebugCounterIds = std::make_shared<SwitchCounterIds>(switchRid, supportedIds);

    m_switchDebugCounterIdsMap.emplace(switchVid, switchDebugCounterIds);

    addCollectCountersHandler(SWITCH_DEBUG_COUNTER_ID_LIST, &FlexCounter::collectSwitchDebugCounters);
}

void FlexCounter::setMACsecFlowCounterList(
        _In_ sai_object_id_t macsecFlowVid,
        _In_ sai_object_id_t macsecFlowRid,
        _In_ const std::vector<sai_macsec_flow_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    auto it = m_macsecFlowCounterIdsMap.find(macsecFlowVid);

    if (it != m_macsecFlowCounterIdsMap.end())
    {
        it->second->m_macsecFlowCounterIds = counterIds;
        return;
    }

    auto macsecFlowCounterIds = std::make_shared<MACsecFlowCounterIds>(macsecFlowRid, counterIds);

    m_macsecFlowCounterIdsMap.emplace(macsecFlowVid, macsecFlowCounterIds);

    addCollectCountersHandler(MACSEC_FLOW_COUNTER_ID_LIST, &FlexCounter::collectMACsecFlowCounters);
}

void FlexCounter::setMACsecSACounterList(
        _In_ sai_object_id_t macsecSAVid,
        _In_ sai_object_id_t macsecSARid,
        _In_ const std::vector<sai_macsec_sa_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    auto it = m_macsecSACounterIdsMap.find(macsecSAVid);

    if (it != m_macsecSACounterIdsMap.end())
    {
        it->second->m_macsecSACounterIds = counterIds;
        return;
    }

    auto macsecSACounterIds = std::make_shared<MACsecSACounterIds>(macsecSARid, counterIds);

    m_macsecSACounterIdsMap.emplace(macsecSAVid, macsecSACounterIds);

    addCollectCountersHandler(MACSEC_SA_COUNTER_ID_LIST, &FlexCounter::collectMACsecSACounters);
}

void FlexCounter::setPriorityGroupAttrList(
        _In_ sai_object_id_t priorityGroupVid,
        _In_ sai_object_id_t priorityGroupRid,
        _In_ const std::vector<sai_ingress_priority_group_attr_t> &attrIds)
{
    SWSS_LOG_ENTER();

    auto it = m_priorityGroupAttrIdsMap.find(priorityGroupVid);

    if (it != m_priorityGroupAttrIdsMap.end())
    {
        it->second->priorityGroupAttrIds = attrIds;
        return;
    }

    auto priorityGroupAttrIds = std::make_shared<IngressPriorityGroupAttrIds>(priorityGroupRid, attrIds);

    m_priorityGroupAttrIdsMap.emplace(priorityGroupVid, priorityGroupAttrIds);

    addCollectCountersHandler(PG_ATTR_ID_LIST, &FlexCounter::collectPriorityGroupAttrs);
}

void FlexCounter::setMACsecSAAttrList(
        _In_ sai_object_id_t macsecSAVid,
        _In_ sai_object_id_t macsecSARid,
        _In_ const std::vector<sai_macsec_sa_attr_t> &attrIds)
{
    SWSS_LOG_ENTER();

    auto it = m_macsecSAAttrIdsMap.find(macsecSAVid);

    if (it != m_macsecSAAttrIdsMap.end())
    {
        it->second->m_macsecSAAttrIds = attrIds;
        return;
    }

    auto macsecSAAttrIds = std::make_shared<MACsecSAAttrIds>(macsecSARid, attrIds);

    m_macsecSAAttrIdsMap.emplace(macsecSAVid, macsecSAAttrIds);

    addCollectCountersHandler(MACSEC_SA_ATTR_ID_LIST, &FlexCounter::collectMACsecSAAttrs);
}

void FlexCounter::setAclCounterAttrList(
        _In_ sai_object_id_t aclCounterVid,
        _In_ sai_object_id_t aclCounterRid,
        _In_ const std::vector<sai_acl_counter_attr_t> &attrIds)
{
    SWSS_LOG_ENTER();

    auto it = m_aclCounterAttrIdsMap.find(aclCounterVid);

    if (it != m_aclCounterAttrIdsMap.end())
    {
        it->second->m_aclCounterAttrIds = attrIds;
        return;
    }

    auto aclCounterAttrIds = std::make_shared<AclCounterAttrIds>(aclCounterRid, attrIds);

    m_aclCounterAttrIdsMap.emplace(aclCounterVid, aclCounterAttrIds);

    addCollectCountersHandler(ACL_COUNTER_ATTR_ID_LIST, &FlexCounter::collectAclCounterAttrs);
}

void FlexCounter::setRifCounterList(
        _In_ sai_object_id_t rifVid,
        _In_ sai_object_id_t rifRid,
        _In_ const std::vector<sai_router_interface_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedRifCounters(rifRid);

    // Remove unsupported counters
    std::vector<sai_router_interface_stat_t> supportedIds;

    for (auto &counter : counterIds)
    {
        if (isRifCounterSupported(counter))
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.empty())
    {
        SWSS_LOG_NOTICE("Router interface %s does not have supported counters", sai_serialize_object_id(rifRid).c_str());
        return;
    }

    auto it = m_rifCounterIdsMap.find(rifVid);

    if (it != m_rifCounterIdsMap.end())
    {
        it->second->rifCounterIds = supportedIds;
        return;
    }

    auto rifCounterIds = std::make_shared<RifCounterIds>(rifRid, supportedIds);

    m_rifCounterIdsMap.emplace(rifVid, rifCounterIds);

    addCollectCountersHandler(RIF_COUNTER_ID_LIST, &FlexCounter::collectRifCounters);
}

void FlexCounter::setBufferPoolCounterList(
        _In_ sai_object_id_t bufferPoolVid,
        _In_ sai_object_id_t bufferPoolId,
        _In_ const std::vector<sai_buffer_pool_stat_t> &counterIds,
        _In_ const std::string &statsMode)
{
    SWSS_LOG_ENTER();

    sai_stats_mode_t bufferPoolStatsMode = SAI_STATS_MODE_READ_AND_CLEAR;

    if (statsMode == STATS_MODE_READ_AND_CLEAR)
    {
        bufferPoolStatsMode = SAI_STATS_MODE_READ_AND_CLEAR;
    }
    else if (statsMode == STATS_MODE_READ)
    {
        bufferPoolStatsMode = SAI_STATS_MODE_READ;
    }
    else
    {
        SWSS_LOG_WARN("Stats mode %s not supported for flex counter. Using STATS_MODE_READ_AND_CLEAR", statsMode.c_str());
    }

    updateSupportedBufferPoolCounters(bufferPoolId, counterIds, bufferPoolStatsMode);

    // Filter unsupported counters
    std::vector<sai_buffer_pool_stat_t> supportedIds;

    for (auto counterId : counterIds)
    {
        if (isBufferPoolCounterSupported(counterId))
        {
            supportedIds.push_back(counterId);
        }
    }

    if (supportedIds.size() == 0)
    {
        SWSS_LOG_NOTICE("Buffer pool %s does not has supported counters", sai_serialize_object_id(bufferPoolId).c_str());
        return;
    }

    auto it = m_bufferPoolCounterIdsMap.find(bufferPoolVid);

    if (it != m_bufferPoolCounterIdsMap.end())
    {
        it->second->bufferPoolCounterIds = supportedIds;
        return;
    }

    auto bufferPoolCounterIds = std::make_shared<BufferPoolCounterIds>(bufferPoolId, supportedIds, bufferPoolStatsMode);

    m_bufferPoolCounterIdsMap.emplace(bufferPoolVid, bufferPoolCounterIds);

    addCollectCountersHandler(BUFFER_POOL_COUNTER_ID_LIST, &FlexCounter::collectBufferPoolCounters);
}

void FlexCounter::setTunnelCounterList(
        _In_ sai_object_id_t tunnelVid,
        _In_ sai_object_id_t tunnelRid,
        _In_ const std::vector<sai_tunnel_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedTunnelCounters(tunnelRid, counterIds);

    // Remove unsupported counters
    std::vector<sai_tunnel_stat_t> supportedIds;

    for (auto &counter : counterIds)
    {
        if (isTunnelCounterSupported(counter))
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.empty())
    {
        SWSS_LOG_NOTICE("Tunnel %s does not have supported counters", sai_serialize_object_id(tunnelRid).c_str());
        return;
    }

    auto it = m_tunnelCounterIdsMap.find(tunnelVid);

    if (it != m_tunnelCounterIdsMap.end())
    {
        it->second->m_tunnelCounterIds = supportedIds;
        return;
    }

    auto tunnelCounterIds = std::make_shared<TunnelCounterIds>(tunnelRid, supportedIds);

    m_tunnelCounterIdsMap.emplace(tunnelVid, tunnelCounterIds);

    addCollectCountersHandler(TUNNEL_COUNTER_ID_LIST, &FlexCounter::collectTunnelCounters);
}

void FlexCounter::setFlowCounterList(
        _In_ sai_object_id_t counterVid,
        _In_ sai_object_id_t counterRid,
        _In_ const std::vector<sai_counter_stat_t>& counterIds)
{
    SWSS_LOG_ENTER();

    updateSupportedFlowCounters(counterRid, counterIds);

    std::vector<sai_counter_stat_t> supportedIds;
    for (auto &counter : counterIds)
    {
        if (m_supportedFlowCounters.count(counter) != 0)
        {
            supportedIds.push_back(counter);
        }
    }

    if (supportedIds.empty())
    {
        SWSS_LOG_NOTICE("Flow counter %s does not have supported counters", sai_serialize_object_id(counterRid).c_str());
        return;
    }

    auto it = m_flowCounterIdsMap.find(counterVid);
    if (it != m_flowCounterIdsMap.end())
    {
        it->second->flowCounterIds = supportedIds;
        return;
    }

    auto flowCounterIds = std::make_shared<FlowCounterIds>(counterRid, supportedIds);

    m_flowCounterIdsMap.emplace(counterVid, flowCounterIds);

    addCollectCountersHandler(FLOW_COUNTER_ID_LIST, &FlexCounter::collectFlowCounters);
}

void FlexCounter::removePort(
        _In_ sai_object_id_t portVid)
{
    SWSS_LOG_ENTER();

    auto it = m_portCounterIdsMap.find(portVid);

    if (it == m_portCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting port %s",
                sai_serialize_object_id(portVid).c_str());
        return;
    }

    m_portCounterIdsMap.erase(it);

    if (m_portCounterIdsMap.empty())
    {
        removeCollectCountersHandler(PORT_COUNTER_ID_LIST);
    }
}

void FlexCounter::removePortDebugCounters(
        _In_ sai_object_id_t portVid)
{
    SWSS_LOG_ENTER();

    auto it = m_portDebugCounterIdsMap.find(portVid);

    if (it == m_portDebugCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting port debug counter %s",
                sai_serialize_object_id(portVid).c_str());
        return;
    }

    m_portDebugCounterIdsMap.erase(it);

    if (m_portDebugCounterIdsMap.empty())
    {
        removeCollectCountersHandler(PORT_DEBUG_COUNTER_ID_LIST);
    }
}

void FlexCounter::removeQueue(
        _In_ sai_object_id_t queueVid)
{
    SWSS_LOG_ENTER();

    bool found = false;

    auto counterIter = m_queueCounterIdsMap.find(queueVid);

    if (counterIter != m_queueCounterIdsMap.end())
    {
        m_queueCounterIdsMap.erase(counterIter);

        if (m_queueCounterIdsMap.empty())
        {
            removeCollectCountersHandler(QUEUE_COUNTER_ID_LIST);
        }

        found = true;
    }

    auto attrIter = m_queueAttrIdsMap.find(queueVid);

    if (attrIter != m_queueAttrIdsMap.end())
    {
        m_queueAttrIdsMap.erase(attrIter);

        if (m_queueAttrIdsMap.empty())
        {
            removeCollectCountersHandler(QUEUE_ATTR_ID_LIST);
        }

        found = true;
    }

    if (!found)
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting queue %s",
                sai_serialize_object_id(queueVid).c_str());
    }
}

void FlexCounter::removePriorityGroup(
        _In_ sai_object_id_t priorityGroupVid)
{
    SWSS_LOG_ENTER();

    bool found = false;

    auto counterIter = m_priorityGroupCounterIdsMap.find(priorityGroupVid);

    if (counterIter != m_priorityGroupCounterIdsMap.end())
    {
        m_priorityGroupCounterIdsMap.erase(counterIter);

        if (m_priorityGroupCounterIdsMap.empty())
        {
            removeCollectCountersHandler(PG_COUNTER_ID_LIST);
        }

        found = true;
    }

    auto attrIter = m_priorityGroupAttrIdsMap.find(priorityGroupVid);

    if (attrIter != m_priorityGroupAttrIdsMap.end())
    {
        m_priorityGroupAttrIdsMap.erase(attrIter);

        if (m_priorityGroupAttrIdsMap.empty())
        {
            removeCollectCountersHandler(PG_ATTR_ID_LIST);
        }

        found = true;
    }

    if (!found)
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting PG %s",
                sai_serialize_object_id(priorityGroupVid).c_str());
        return;
    }
}

void FlexCounter::removeMACsecFlow(
        _In_ sai_object_id_t macsecFlowVid)
{
    SWSS_LOG_ENTER();

    bool found = false;

    auto counterIter = m_macsecFlowCounterIdsMap.find(macsecFlowVid);

    if (counterIter != m_macsecFlowCounterIdsMap.end())
    {
        m_macsecFlowCounterIdsMap.erase(counterIter);

        if (m_macsecFlowCounterIdsMap.empty())
        {
            removeCollectCountersHandler(MACSEC_FLOW_COUNTER_ID_LIST);
        }

        found = true;
    }

    if (!found)
    {
        SWSS_LOG_WARN("Trying to remove nonexisting MACsec Flow %s",
                sai_serialize_object_id(macsecFlowVid).c_str());
    }
}

void FlexCounter::removeMACsecSA(
        _In_ sai_object_id_t macsecSAVid)
{
    SWSS_LOG_ENTER();

    bool found = false;

    auto counterIter = m_macsecSACounterIdsMap.find(macsecSAVid);

    if (counterIter != m_macsecSACounterIdsMap.end())
    {
        m_macsecSACounterIdsMap.erase(counterIter);

        if (m_macsecSACounterIdsMap.empty())
        {
            removeCollectCountersHandler(MACSEC_SA_COUNTER_ID_LIST);
        }

        found = true;
    }

    auto attrIter = m_macsecSAAttrIdsMap.find(macsecSAVid);

    if (attrIter != m_macsecSAAttrIdsMap.end())
    {
        m_macsecSAAttrIdsMap.erase(attrIter);

        if (m_macsecSAAttrIdsMap.empty())
        {
            removeCollectCountersHandler(MACSEC_SA_ATTR_ID_LIST);
        }

        found = true;
    }

    if (!found)
    {
        SWSS_LOG_WARN("Trying to remove nonexisting MACsec SA %s",
                sai_serialize_object_id(macsecSAVid).c_str());
    }
}

void FlexCounter::removeAclCounter(
        _In_ sai_object_id_t aclCounterVid)
{
    SWSS_LOG_ENTER();

    auto itr = m_aclCounterAttrIdsMap.find(aclCounterVid);

    if (itr != m_aclCounterAttrIdsMap.end())
    {
        m_aclCounterAttrIdsMap.erase(itr);

        if (m_aclCounterAttrIdsMap.empty())
        {
            removeCollectCountersHandler(ACL_COUNTER_ATTR_ID_LIST);
        }
    }
    else
    {
        SWSS_LOG_WARN("Trying to remove nonexisting ACL counter %s",
                sai_serialize_object_id(aclCounterVid).c_str());
    }
}

void FlexCounter::removeFlowCounter(
        _In_ sai_object_id_t counterVid)
{
    SWSS_LOG_ENTER();

    auto it = m_flowCounterIdsMap.find(counterVid);

    if (it == m_flowCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting flow counter from Id 0x%" PRIx64, counterVid);
        return;
    }

    swss::DBConnector db(m_dbCounters, 0);
    swss::RedisPipeline pipeline(&db);
    swss::Table countersTable(&pipeline, COUNTERS_TABLE, false);
    swss::Table ratesTable(&pipeline, RATES_TABLE, false);

    // Remove counter and rate entries from COUNTER DB to avoid resource leak
    std::string counterVidStr = sai_serialize_object_id(counterVid);
    countersTable.del(counterVidStr);
    ratesTable.del(counterVidStr);
    ratesTable.del(counterVidStr + ":TRAP");

    m_flowCounterIdsMap.erase(it);

    if (m_flowCounterIdsMap.empty())
    {
        removeCollectCountersHandler(FLOW_COUNTER_ID_LIST);
    }
}

void FlexCounter::removeRif(
        _In_ sai_object_id_t rifVid)
{
    SWSS_LOG_ENTER();

    auto it = m_rifCounterIdsMap.find(rifVid);

    if (it == m_rifCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting router interface counter from Id 0x%" PRIx64, rifVid);
        return;
    }

    m_rifCounterIdsMap.erase(it);

    if (m_rifCounterIdsMap.empty())
    {
        removeCollectCountersHandler(RIF_COUNTER_ID_LIST);
    }
}

void FlexCounter::removeBufferPool(
        _In_ sai_object_id_t bufferPoolVid)
{
    SWSS_LOG_ENTER();

    bool found = false;

    auto it = m_bufferPoolCounterIdsMap.find(bufferPoolVid);

    if (it != m_bufferPoolCounterIdsMap.end())
    {
        m_bufferPoolCounterIdsMap.erase(it);

        if (m_bufferPoolCounterIdsMap.empty())
        {
            removeCollectCountersHandler(BUFFER_POOL_COUNTER_ID_LIST);
        }
        found = true;
    }

    if (!found)
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting buffer pool 0x%" PRIx64 " from flex counter %s", bufferPoolVid, m_instanceId.c_str());
    }
}

void FlexCounter::removeSwitchDebugCounters(
        _In_ sai_object_id_t switchVid)
{
    SWSS_LOG_ENTER();

    auto it = m_switchDebugCounterIdsMap.find(switchVid);

    if (it == m_switchDebugCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting switch debug counter Ids 0x%" PRIx64, switchVid);
        return;
    }

    m_switchDebugCounterIdsMap.erase(it);

    if (m_switchDebugCounterIdsMap.empty())
    {
        removeCollectCountersHandler(SWITCH_DEBUG_COUNTER_ID_LIST);
    }
}

void FlexCounter::removeTunnel(
        _In_ sai_object_id_t tunnelVid)
{
    SWSS_LOG_ENTER();

    auto it = m_tunnelCounterIdsMap.find(tunnelVid);

    if (it == m_tunnelCounterIdsMap.end())
    {
        SWSS_LOG_NOTICE("Trying to remove nonexisting tunnel counter from Id 0x%" PRIx64, tunnelVid);
        return;
    }

    m_tunnelCounterIdsMap.erase(it);

    if (m_tunnelCounterIdsMap.empty())
    {
        removeCollectCountersHandler(TUNNEL_COUNTER_ID_LIST);
    }
}

void FlexCounter::checkPluginRegistered(
        _In_ const std::string& sha) const
{
    SWSS_LOG_ENTER();

    if (
            m_portPlugins.find(sha) != m_portPlugins.end() ||
            m_rifPlugins.find(sha) != m_rifPlugins.end() ||
            m_queuePlugins.find(sha) != m_queuePlugins.end() ||
            m_priorityGroupPlugins.find(sha) != m_priorityGroupPlugins.end() ||
            m_bufferPoolPlugins.find(sha) != m_bufferPoolPlugins.end() ||
            m_tunnelPlugins.find(sha) != m_tunnelPlugins.end() ||
            m_flowCounterPlugins.find(sha) != m_flowCounterPlugins.end()
       )
    {
        SWSS_LOG_ERROR("Plugin %s already registered", sha.c_str());
    }
}

void FlexCounter::addPortCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_portPlugins.insert(sha);

    SWSS_LOG_NOTICE("Port counters plugin %s registered", sha.c_str());
}

void FlexCounter::addRifCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_rifPlugins.insert(sha);

    SWSS_LOG_NOTICE("Rif counters plugin %s registered", sha.c_str());
}

void FlexCounter::addQueueCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_queuePlugins.insert(sha);

    SWSS_LOG_NOTICE("Queue counters plugin %s registered", sha.c_str());
}

void FlexCounter::addFlowCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_flowCounterPlugins.insert(sha);

    SWSS_LOG_NOTICE("Flow counters plugin %s registered", sha.c_str());
}

void FlexCounter::addPriorityGroupCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_priorityGroupPlugins.insert(sha);

    SWSS_LOG_NOTICE("Priority group counters plugin %s registered", sha.c_str());
}

void FlexCounter::addBufferPoolCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_bufferPoolPlugins.insert(sha);

    SWSS_LOG_NOTICE("Buffer pool counters plugin %s registered", sha.c_str());
}

void FlexCounter::addTunnelCounterPlugin(
        _In_ const std::string& sha)
{
    SWSS_LOG_ENTER();

    checkPluginRegistered(sha);

    m_tunnelPlugins.insert(sha);

    SWSS_LOG_NOTICE("Tunnel counters plugin %s registered", sha.c_str());
}

void FlexCounter::removeCounterPlugins()
{
    MUTEX;

    SWSS_LOG_ENTER();

    m_queuePlugins.clear();
    m_portPlugins.clear();
    m_rifPlugins.clear();
    m_priorityGroupPlugins.clear();
    m_bufferPoolPlugins.clear();
    m_tunnelPlugins.clear();
    m_flowCounterPlugins.clear();

    m_isDiscarded = true;
}

void FlexCounter::addCounterPlugin(
        _In_ const std::vector<swss::FieldValueTuple>& values)
{
    MUTEX;

    SWSS_LOG_ENTER();

    m_isDiscarded = false;

    for (auto& fvt: values)
    {
        auto& field = fvField(fvt);
        auto& value = fvValue(fvt);

        auto shaStrings = swss::tokenize(value, ',');

        if (field == POLL_INTERVAL_FIELD)
        {
            setPollInterval(stoi(value));
        }
        else if (field == FLEX_COUNTER_STATUS_FIELD)
        {
            setStatus(value);
        }
        else if (field == STATS_MODE_FIELD)
        {
            setStatsMode(value);
        }
        else if (field == QUEUE_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addQueueCounterPlugin(sha);
            }
        }
        else if (field == PG_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addPriorityGroupCounterPlugin(sha);
            }
        }
        else if (field == PORT_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addPortCounterPlugin(sha);
            }
        }
        else if (field == RIF_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addRifCounterPlugin(sha);
            }
        }
        else if (field == BUFFER_POOL_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addBufferPoolCounterPlugin(sha);
            }
        }
        else if (field == TUNNEL_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addTunnelCounterPlugin(sha);
            }
        }
        else if (field == FLOW_COUNTER_PLUGIN_FIELD)
        {
            for (auto& sha: shaStrings)
            {
                addFlowCounterPlugin(sha);
            }
        }
        else
        {
            SWSS_LOG_ERROR("Field is not supported %s", field.c_str());
        }
    }

    // notify thread to start polling
    notifyPoll();
}

bool FlexCounter::isEmpty()
{
    MUTEX;

    SWSS_LOG_ENTER();

    return allIdsEmpty() && allPluginsEmpty();
}

bool FlexCounter::isDiscarded()
{
    SWSS_LOG_ENTER();

    return isEmpty() && m_isDiscarded;
}

bool FlexCounter::allIdsEmpty() const
{
    SWSS_LOG_ENTER();

    return m_priorityGroupCounterIdsMap.empty() &&
        m_priorityGroupAttrIdsMap.empty() &&
        m_queueCounterIdsMap.empty() &&
        m_queueAttrIdsMap.empty() &&
        m_portCounterIdsMap.empty() &&
        m_portDebugCounterIdsMap.empty() &&
        m_rifCounterIdsMap.empty() &&
        m_bufferPoolCounterIdsMap.empty() &&
        m_switchDebugCounterIdsMap.empty() &&
        m_aclCounterAttrIdsMap.empty() &&
        m_tunnelCounterIdsMap.empty() &&
        m_flowCounterIdsMap.empty() &&
        m_macsecFlowCounterIdsMap.empty() &&
        m_macsecSACounterIdsMap.empty() &&
        m_macsecSAAttrIdsMap.empty();
}

bool FlexCounter::allPluginsEmpty() const
{
    SWSS_LOG_ENTER();

    return m_priorityGroupPlugins.empty() &&
           m_queuePlugins.empty() &&
           m_portPlugins.empty() &&
           m_rifPlugins.empty() &&
           m_bufferPoolPlugins.empty() &&
           m_tunnelPlugins.empty() &&
           m_flowCounterPlugins.empty();
}

bool FlexCounter::isPortCounterSupported(sai_port_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedPortCounters.count(counter) != 0;
}

bool FlexCounter::isQueueCounterSupported(
        _In_ sai_queue_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedQueueCounters.count(counter) != 0;
}

bool FlexCounter::isPriorityGroupCounterSupported(
        _In_ sai_ingress_priority_group_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedPriorityGroupCounters.count(counter) != 0;
}

bool FlexCounter::isRifCounterSupported(
        _In_ sai_router_interface_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedRifCounters.count(counter) != 0;
}

bool FlexCounter::isBufferPoolCounterSupported(
        _In_ sai_buffer_pool_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedBufferPoolCounters.count(counter) != 0;
}

bool FlexCounter::isTunnelCounterSupported(
        _In_ sai_tunnel_stat_t counter) const
{
    SWSS_LOG_ENTER();

    return m_supportedTunnelCounters.count(counter) != 0;
}

bool FlexCounter::isStatsModeSupported(
        _In_ uint32_t statsMode,
        _In_ sai_stats_mode_t statCapability)
{
    SWSS_LOG_ENTER();

    return statsMode & statCapability;
}

void FlexCounter::collectCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    for (const auto &it : m_collectCountersHandlers)
    {
        (this->*(it.second))(countersTable);
    }

    countersTable.flush();
}

void FlexCounter::collectPortCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered port
    for (const auto &kv: m_portCounterIdsMap)
    {
        const auto &portVid = kv.first;
        const auto &portId = kv.second->portId;
        const auto &portCounterIds = kv.second->portCounterIds;

        std::vector<uint64_t> portStats(portCounterIds.size());

        // Get port stats
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_PORT,
                portId,
                static_cast<uint32_t>(portCounterIds.size()),
                (const sai_stat_id_t *)portCounterIds.data(),
                portStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of port 0x%" PRIx64 ": %d", portId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != portCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_port_stat(portCounterIds[i]);

            values.emplace_back(counterName, std::to_string(portStats[i]));
        }

        // Write counters to DB
        std::string portVidStr = sai_serialize_object_id(portVid);

        countersTable.set(portVidStr, values, "");
    }
}

void FlexCounter::collectPortDebugCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered port
    for (const auto &kv: m_portDebugCounterIdsMap)
    {
        const auto &portVid = kv.first;
        const auto &portId = kv.second->portId;
        const auto &portCounterIds = kv.second->portCounterIds;

        std::vector<uint64_t> portStats(portCounterIds.size());

        // Get port stats
        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_PORT,
                portId,
                static_cast<uint32_t>(portCounterIds.size()),
                (const sai_stat_id_t *)portCounterIds.data(),
                SAI_STATS_MODE_READ,
                portStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of port 0x%" PRIx64 ": %d", portId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != portCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_port_stat(portCounterIds[i]);

            values.emplace_back(counterName, std::to_string(portStats[i]));
        }

        // Write counters to DB
        std::string portVidStr = sai_serialize_object_id(portVid);

        countersTable.set(portVidStr, values, "");
    }
}

void FlexCounter::collectQueueCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered queue
    for (const auto &kv: m_queueCounterIdsMap)
    {
        const auto &queueVid = kv.first;
        const auto &queueId = kv.second->queueId;
        const auto &queueCounterIds = kv.second->queueCounterIds;

        std::vector<uint64_t> queueStats(queueCounterIds.size());

        // Get queue stats
        sai_status_t status = -1;
        // TODO: replace if with get_queue_stats_ext() call when it is fully supported
        // Example:
        // sai_status_t status = m_vendorSai->getStatsExt(
        //         queueId,
        //         static_cast<uint32_t>(queueCounterIds.size()),
        //         queueCounterIds.data(),
        //         m_statsMode,
        //         queueStats.data());
        status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_QUEUE,
                queueId,
                static_cast<uint32_t>(queueCounterIds.size()),
                (const sai_stat_id_t *)queueCounterIds.data(),
                queueStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("%s: failed to get stats of queue 0x%" PRIx64 ": %d", m_instanceId.c_str(), queueVid, status);
            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_QUEUE,
                    queueId,
                    static_cast<uint32_t>(queueCounterIds.size()),
                    (const sai_stat_id_t *)queueCounterIds.data());

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("%s: failed to clear stats of queue 0x%" PRIx64 ": %d", m_instanceId.c_str(), queueVid, status);
                continue;
            }
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != queueCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_queue_stat(queueCounterIds[i]);

            values.emplace_back(counterName, std::to_string(queueStats[i]));
        }

        // Write counters to DB
        std::string queueVidStr = sai_serialize_object_id(queueVid);

        countersTable.set(queueVidStr, values, "");
    }
}

void FlexCounter::collectQueueAttrs(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect attrs for every registered queue
    for (const auto &kv: m_queueAttrIdsMap)
    {
        const auto &queueVid = kv.first;
        const auto &queueId = kv.second->queueId;
        const auto &queueAttrIds = kv.second->queueAttrIds;

        std::vector<sai_attribute_t> queueAttr(queueAttrIds.size());

        for (size_t i = 0; i < queueAttrIds.size(); i++)
        {
            queueAttr[i].id = queueAttrIds[i];
        }

        // Get queue attr
        sai_status_t status = m_vendorSai->get(
                SAI_OBJECT_TYPE_QUEUE,
                queueId,
                static_cast<uint32_t>(queueAttrIds.size()),
                queueAttr.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get attr of queue 0x%" PRIx64 ": %d", queueVid, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != queueAttrIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_queue_attr(queueAttrIds[i]);

            auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_QUEUE, queueAttr[i].id);

            values.emplace_back(counterName, sai_serialize_attr_value(*meta, queueAttr[i]));
        }
        // Write counters to DB
        std::string queueVidStr = sai_serialize_object_id(queueVid);

        countersTable.set(queueVidStr, values, "");
    }
}

void FlexCounter::collectPriorityGroupCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered ingress priority group
    for (const auto &kv: m_priorityGroupCounterIdsMap)
    {
        const auto &priorityGroupVid = kv.first;
        const auto &priorityGroupId = kv.second->priorityGroupId;
        const auto &priorityGroupCounterIds = kv.second->priorityGroupCounterIds;

        std::vector<uint64_t> priorityGroupStats(priorityGroupCounterIds.size());

        // Get PG stats
        sai_status_t status = -1;
        // TODO: replace if with get_ingress_priority_group_stats_ext() call when it is fully supported
        status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                priorityGroupId,
                static_cast<uint32_t>(priorityGroupCounterIds.size()),
                (const sai_stat_id_t *)priorityGroupCounterIds.data(),
                priorityGroupStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("%s: failed to get %ld/%ld stats of PG 0x%" PRIx64 ": %d",
                    m_instanceId.c_str(),
                    priorityGroupCounterIds.size(),
                    priorityGroupStats.size(),
                    priorityGroupVid,
                    status);
            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                    priorityGroupId,
                    static_cast<uint32_t>(priorityGroupCounterIds.size()),
                    (const sai_stat_id_t *)priorityGroupCounterIds.data());

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("%s: failed to clear %ld/%ld stats of PG 0x%" PRIx64 ": %d",
                        m_instanceId.c_str(),
                        priorityGroupCounterIds.size(),
                        priorityGroupStats.size(),
                        priorityGroupVid,
                        status);
                continue;
            }
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != priorityGroupCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_ingress_priority_group_stat(priorityGroupCounterIds[i]);

            values.emplace_back(counterName, std::to_string(priorityGroupStats[i]));
        }

        // Write counters to DB
        std::string priorityGroupVidStr = sai_serialize_object_id(priorityGroupVid);

        countersTable.set(priorityGroupVidStr, values, "");
    }
}

void FlexCounter::collectSwitchDebugCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered port
    for (const auto &kv: m_switchDebugCounterIdsMap)
    {
        const auto &switchVid = kv.first;
        const auto &switchId = kv.second->switchId;
        const auto &switchCounterIds = kv.second->switchCounterIds;

        std::vector<uint64_t> switchStats(switchCounterIds.size());

        // Get port stats
        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_SWITCH,
                switchId,
                static_cast<uint32_t>(switchCounterIds.size()),
                (const sai_stat_id_t *)switchCounterIds.data(),
                SAI_STATS_MODE_READ,
                switchStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of port 0x%" PRIx64 ": %d", switchId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != switchCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_switch_stat(switchCounterIds[i]);

            values.emplace_back(counterName, std::to_string(switchStats[i]));
        }

        // Write counters to DB
        std::string switchVidStr = sai_serialize_object_id(switchVid);

        countersTable.set(switchVidStr, values, "");
    }
}

void FlexCounter::collectFlowCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered flow counter
    for (const auto &kv: m_flowCounterIdsMap)
    {
        const auto &counterVid = kv.first;
        const auto &counterRId = kv.second->counterId;
        const auto &counterIds = kv.second->flowCounterIds;

        std::vector<uint64_t> stats(counterIds.size());

        // Get flow counter stats
        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_COUNTER,
                counterRId,
                static_cast<uint32_t>(counterIds.size()),
                (const sai_stat_id_t *)counterIds.data(),
                SAI_STATS_MODE_READ,
                stats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of flow counter 0x%" PRIx64 ": %d", counterRId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != counterIds.size(); i++)
        {
            values.emplace_back(sai_serialize_counter_stat(counterIds[i]), std::to_string(stats[i]));
        }

        // Write counters to DB
        std::string counterVidStr = sai_serialize_object_id(counterVid);

        countersTable.set(counterVidStr, values, "");
    }
}

void FlexCounter::collectPriorityGroupAttrs(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect attrs for every registered priority group
    for (const auto &kv: m_priorityGroupAttrIdsMap)
    {
        const auto &priorityGroupVid = kv.first;
        const auto &priorityGroupId = kv.second->priorityGroupId;
        const auto &priorityGroupAttrIds = kv.second->priorityGroupAttrIds;

        std::vector<sai_attribute_t> priorityGroupAttr(priorityGroupAttrIds.size());

        for (size_t i = 0; i < priorityGroupAttrIds.size(); i++)
        {
            priorityGroupAttr[i].id = priorityGroupAttrIds[i];
        }

        // Get PG attr
        sai_status_t status = m_vendorSai->get(
                SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                priorityGroupId,
                static_cast<uint32_t>(priorityGroupAttrIds.size()),
                priorityGroupAttr.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get attr of PG 0x%" PRIx64 ": %d", priorityGroupVid, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != priorityGroupAttrIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_ingress_priority_group_attr(priorityGroupAttrIds[i]);

            auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, priorityGroupAttr[i].id);

            values.emplace_back(counterName, sai_serialize_attr_value(*meta, priorityGroupAttr[i]));
        }
        // Write counters to DB
        std::string priorityGroupVidStr = sai_serialize_object_id(priorityGroupVid);

        countersTable.set(priorityGroupVidStr, values, "");
    }
}

void FlexCounter::collectMACsecFlowCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered MACsec Flow
    for (const auto &kv: m_macsecFlowCounterIdsMap)
    {
        const auto &macsecFlowVid = kv.first;
        const auto &macsecFlowRid = kv.second->m_macsecFlowId;
        const auto &macsecFlowCounterIds = kv.second->m_macsecFlowCounterIds;

        std::vector<uint64_t> macsecFlowStats(macsecFlowCounterIds.size());

        // Get MACsec Flow stats
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_MACSEC_FLOW,
                macsecFlowRid,
                static_cast<uint32_t>(macsecFlowCounterIds.size()),
                (const sai_stat_id_t *)macsecFlowCounterIds.data(),
                macsecFlowStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR(
                "Failed to get stats of MACsec Flow %s: %s",
                sai_serialize_object_id(macsecFlowVid).c_str(),
                sai_serialize_status(status).c_str());
            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_MACSEC_FLOW,
                    macsecFlowRid,
                    static_cast<uint32_t>(macsecFlowCounterIds.size()),
                    (const sai_stat_id_t *)macsecFlowCounterIds.data());

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR(
                    "Failed to clear stats of MACsec Flow %s: %s",
                    sai_serialize_object_id(macsecFlowVid).c_str(),
                    sai_serialize_status(status).c_str());
                continue;
            }
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != macsecFlowCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_macsec_flow_stat(macsecFlowCounterIds[i]);

            values.emplace_back(counterName, std::to_string(macsecFlowStats[i]));
        }

        // Write counters to DB
        std::string macsecFlowVidStr = sai_serialize_object_id(macsecFlowVid);

        countersTable.set(macsecFlowVidStr, values, "");
    }
}

void FlexCounter::collectMACsecSACounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered MACsec SA
    for (const auto &kv: m_macsecSACounterIdsMap)
    {
        const auto &macsecSAVid = kv.first;
        const auto &macsecSARid = kv.second->m_macsecSAId;
        const auto &macsecSACounterIds = kv.second->m_macsecSACounterIds;

        std::vector<uint64_t> macsecSAStats(macsecSACounterIds.size());

        // Get MACsec SA stats
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_MACSEC_SA,
                macsecSARid,
                static_cast<uint32_t>(macsecSACounterIds.size()),
                (const sai_stat_id_t *)macsecSACounterIds.data(),
                macsecSAStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR(
                "Failed to get stats of MACsec SA %s: %s",
                sai_serialize_object_id(macsecSAVid).c_str(),
                sai_serialize_status(status).c_str());
            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_MACSEC_SA,
                    macsecSARid,
                    static_cast<uint32_t>(macsecSACounterIds.size()),
                    (const sai_stat_id_t *)macsecSACounterIds.data());

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR(
                    "Failed to clear stats of MACsec SA %s: %s",
                    sai_serialize_object_id(macsecSAVid).c_str(),
                    sai_serialize_status(status).c_str());
                continue;
            }
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != macsecSACounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_macsec_sa_stat(macsecSACounterIds[i]);

            values.emplace_back(counterName, std::to_string(macsecSAStats[i]));
        }

        // Write counters to DB
        std::string macsecSAVidStr = sai_serialize_object_id(macsecSAVid);

        countersTable.set(macsecSAVidStr, values, "");
    }
}

void FlexCounter::collectMACsecSAAttrs(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect attrs for every registered MACsec SA
    for (const auto &kv: m_macsecSAAttrIdsMap)
    {
        const auto &macsecSAVid = kv.first;
        const auto &macsecSARid = kv.second->m_macsecSAId;
        const auto &macsecSAAttrIds = kv.second->m_macsecSAAttrIds;

        std::vector<sai_attribute_t> macsecSAAttrs(macsecSAAttrIds.size());

        for (size_t i = 0; i < macsecSAAttrIds.size(); i++)
        {
            macsecSAAttrs[i].id = macsecSAAttrIds[i];
        }

        // Get MACsec SA attr
        sai_status_t status = m_vendorSai->get(
                SAI_OBJECT_TYPE_MACSEC_SA,
                macsecSARid,
                static_cast<uint32_t>(macsecSAAttrs.size()),
                macsecSAAttrs.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_WARN(
                "Failed to get attr of MACsec SA %s: %s",
                sai_serialize_object_id(macsecSAVid).c_str(),
                sai_serialize_status(status).c_str());
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (const auto& macsecSAAttr : macsecSAAttrs)
        {
            auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_MACSEC_SA, macsecSAAttr.id);
            values.emplace_back(meta->attridname, sai_serialize_attr_value(*meta, macsecSAAttr));
        }

        // Write counters to DB
        std::string macsecSAVidStr = sai_serialize_object_id(macsecSAVid);

        countersTable.set(macsecSAVidStr, values, "");
    }
}

void FlexCounter::collectAclCounterAttrs(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    for (const auto &kv: m_aclCounterAttrIdsMap)
    {
        const auto &aclCounterVid = kv.first;
        const auto &aclCounterRid = kv.second->m_aclCounterId;
        const auto &aclCounterAttrIds = kv.second->m_aclCounterAttrIds;

        std::vector<sai_attribute_t> aclCounterAttrs(aclCounterAttrIds.size());

        for (size_t i = 0; i < aclCounterAttrIds.size(); i++)
        {
            aclCounterAttrs[i].id = aclCounterAttrIds[i];
        }

        sai_status_t status = m_vendorSai->get(
                SAI_OBJECT_TYPE_ACL_COUNTER,
                aclCounterRid,
                static_cast<uint32_t>(aclCounterAttrs.size()),
                aclCounterAttrs.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_WARN(
                "Failed to get attr of ACL counter %s: %s",
                sai_serialize_object_id(aclCounterVid).c_str(),
                sai_serialize_status(status).c_str());
            continue;
        }

        std::vector<swss::FieldValueTuple> values;

        for (const auto& aclCounterAttr : aclCounterAttrs)
        {
            auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_ACL_COUNTER, aclCounterAttr.id);
            if (!meta)
            {
                SWSS_LOG_THROW("Failed to get metadata for SAI_OBJECT_TYPE_ACL_COUNTER");
            }
            values.emplace_back(meta->attridname, sai_serialize_attr_value(*meta, aclCounterAttr));
        }

        // Write counters to DB
        auto aclCounterVidStr = sai_serialize_object_id(aclCounterVid);

        countersTable.set(aclCounterVidStr, values);
    }
}

void FlexCounter::collectRifCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered router interface
    for (const auto &kv: m_rifCounterIdsMap)
    {
        const auto &rifVid = kv.first;
        const auto &rifId = kv.second->rifId;
        const auto &rifCounterIds = kv.second->rifCounterIds;

        std::vector<uint64_t> rifStats(rifCounterIds.size());

        // Get rif stats
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_ROUTER_INTERFACE,
                rifId,
                static_cast<uint32_t>(rifCounterIds.size()),
                (const sai_stat_id_t *)rifCounterIds.data(),
                rifStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of router interface 0x%" PRIx64 ": %d", rifId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != rifCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_router_interface_stat(rifCounterIds[i]);

            values.emplace_back(counterName, std::to_string(rifStats[i]));
        }

        // Write counters to DB
        std::string rifVidStr = sai_serialize_object_id(rifVid);

        countersTable.set(rifVidStr, values, "");
    }
}

void FlexCounter::collectBufferPoolCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered buffer pool
    for (const auto &it : m_bufferPoolCounterIdsMap)
    {
        const auto &bufferPoolVid = it.first;
        const auto &bufferPoolId = it.second->bufferPoolId;
        const auto &bufferPoolCounterIds = it.second->bufferPoolCounterIds;
        const auto &bufferPoolStatsMode = it.second->bufferPoolStatsMode;

        std::vector<uint64_t> bufferPoolStats(bufferPoolCounterIds.size());

        // Get buffer pool stats
        sai_status_t status = -1;

        status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_BUFFER_POOL,
                bufferPoolId,
                static_cast<uint32_t>(bufferPoolCounterIds.size()),
                reinterpret_cast<const sai_stat_id_t *>(bufferPoolCounterIds.data()),
                bufferPoolStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            // Because of stat pre-qualification in setting the buffer pool counter list,
            // it is less likely that we will get a failure return here in polling the stats
            SWSS_LOG_ERROR("%s: failed to get stats of buffer pool %s, rv: %s",
                    m_instanceId.c_str(),
                    sai_serialize_object_id(bufferPoolId).c_str(),
                    sai_serialize_status(status).c_str());
            continue;
        }
        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR || bufferPoolStatsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_BUFFER_POOL,
                    bufferPoolId,
                    static_cast<uint32_t>(bufferPoolCounterIds.size()),
                    reinterpret_cast<const sai_stat_id_t *>(bufferPoolCounterIds.data()));

            if (status != SAI_STATUS_SUCCESS)
            {
                // Because of stat pre-qualification in setting the buffer pool counter list,
                // it is less likely that we will get a failure return here in clearing the stats
                SWSS_LOG_ERROR("%s: failed to clear stats of buffer pool %s, rv: %s",
                        m_instanceId.c_str(),
                        sai_serialize_object_id(bufferPoolId).c_str(),
                        sai_serialize_status(status).c_str());
                continue;
            }
        }

        // Write counter values to DB table
        std::vector<swss::FieldValueTuple> fvTuples;

        for (size_t i = 0; i < bufferPoolCounterIds.size(); ++i)
        {
            fvTuples.emplace_back(sai_serialize_buffer_pool_stat(bufferPoolCounterIds[i]), std::to_string(bufferPoolStats[i]));
        }

        countersTable.set(sai_serialize_object_id(bufferPoolVid), fvTuples);
    }
}

void FlexCounter::collectTunnelCounters(
        _In_ swss::Table &countersTable)
{
    SWSS_LOG_ENTER();

    // Collect stats for every registered tunnel
    for (const auto &kv: m_tunnelCounterIdsMap)
    {
        const auto &tunnelVid = kv.first;
        const auto &tunnelId = kv.second->m_tunnelId;
        const auto &tunnelCounterIds = kv.second->m_tunnelCounterIds;

        std::vector<uint64_t> tunnelStats(tunnelCounterIds.size());

        // Get tunnel stats
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_TUNNEL,
                tunnelId,
                static_cast<uint32_t>(tunnelCounterIds.size()),
                (const sai_stat_id_t *)tunnelCounterIds.data(),
                tunnelStats.data());

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to get stats of tunnel 0x%" PRIx64 ": %d", tunnelId, status);
            continue;
        }

        // Push all counter values to a single vector
        std::vector<swss::FieldValueTuple> values;

        for (size_t i = 0; i != tunnelCounterIds.size(); i++)
        {
            const std::string &counterName = sai_serialize_tunnel_stat(tunnelCounterIds[i]);

            values.emplace_back(counterName, std::to_string(tunnelStats[i]));
        }

        // Write counters to DB
        std::string tunnelVidStr = sai_serialize_object_id(tunnelVid);

        countersTable.set(tunnelVidStr, values, "");
    }
}

void FlexCounter::runPlugins(
        _In_ swss::DBConnector& counters_db)
{
    SWSS_LOG_ENTER();

    const std::vector<std::string> argv =
    {
        std::to_string(counters_db.getDbId()),
        COUNTERS_TABLE,
        std::to_string(m_pollInterval)
    };

    std::vector<std::string> portList;

    portList.reserve(m_portCounterIdsMap.size());

    for (const auto& kv : m_portCounterIdsMap)
    {
        portList.push_back(sai_serialize_object_id(kv.first));
    }

    for (const auto& sha : m_portPlugins)
    {
        runRedisScript(counters_db, sha, portList, argv);
    }

    std::vector<std::string> rifList;
    rifList.reserve(m_rifCounterIdsMap.size());
    for (const auto& kv : m_rifCounterIdsMap)
    {
        rifList.push_back(sai_serialize_object_id(kv.first));
    }
    for (const auto& sha : m_rifPlugins)
    {
        runRedisScript(counters_db, sha, rifList, argv);
    }

    std::vector<std::string> queueList;

    queueList.reserve(m_queueCounterIdsMap.size());

    for (const auto& kv : m_queueCounterIdsMap)
    {
        queueList.push_back(sai_serialize_object_id(kv.first));
    }

    for (const auto& sha : m_queuePlugins)
    {
        runRedisScript(counters_db, sha, queueList, argv);
    }

    std::vector<std::string> flowCounterList;

    flowCounterList.reserve(m_flowCounterIdsMap.size());

    for (const auto& kv : m_flowCounterIdsMap)
    {
        flowCounterList.push_back(sai_serialize_object_id(kv.first));
    }

    for (const auto& sha : m_flowCounterPlugins)
    {
        runRedisScript(counters_db, sha, flowCounterList, argv);
    }

    std::vector<std::string> priorityGroupList;

    priorityGroupList.reserve(m_priorityGroupCounterIdsMap.size());

    for (const auto& kv : m_priorityGroupCounterIdsMap)
    {
        priorityGroupList.push_back(sai_serialize_object_id(kv.first));
    }

    for (const auto& sha : m_priorityGroupPlugins)
    {
        runRedisScript(counters_db, sha, priorityGroupList, argv);
    }

    std::vector<std::string> bufferPoolVids;

    bufferPoolVids.reserve(m_bufferPoolCounterIdsMap.size());

    for (const auto& it : m_bufferPoolCounterIdsMap)
    {
        bufferPoolVids.push_back(sai_serialize_object_id(it.first));
    }

    for (const auto& sha : m_bufferPoolPlugins)
    {
        runRedisScript(counters_db, sha, bufferPoolVids, argv);
    }

    std::vector<std::string> tunnelList;
    tunnelList.reserve(m_tunnelCounterIdsMap.size());
    for (const auto& kv : m_tunnelCounterIdsMap)
    {
        tunnelList.push_back(sai_serialize_object_id(kv.first));
    }
    for (const auto& sha : m_tunnelPlugins)
    {
        runRedisScript(counters_db, sha, tunnelList, argv);
    }
}

void FlexCounter::flexCounterThreadRunFunction()
{
    SWSS_LOG_ENTER();

    swss::DBConnector db(m_dbCounters, 0);
    swss::RedisPipeline pipeline(&db);
    swss::Table countersTable(&pipeline, COUNTERS_TABLE, true);

    while (m_runFlexCounterThread)
    {
        MUTEX;

        if (m_enable && !allIdsEmpty() && (m_pollInterval > 0))
        {
            auto start = std::chrono::steady_clock::now();

            collectCounters(countersTable);

            runPlugins(db);

            auto finish = std::chrono::steady_clock::now();

            uint32_t delay = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count());

            uint32_t correction = delay % m_pollInterval;
            correction = m_pollInterval - correction;
            MUTEX_UNLOCK; // explicit unlock

            SWSS_LOG_DEBUG("End of flex counter thread FC %s, took %d ms", m_instanceId.c_str(), delay);

            std::unique_lock<std::mutex> lk(m_mtxSleep);

            m_cvSleep.wait_for(lk, std::chrono::milliseconds(correction));

            continue;
        }

        MUTEX_UNLOCK; // explicit unlock

        // nothing to collect, wait until notified
        waitPoll();
    }
}

void FlexCounter::startFlexCounterThread()
{
    SWSS_LOG_ENTER();

    m_runFlexCounterThread = true;

    m_flexCounterThread = std::make_shared<std::thread>(&FlexCounter::flexCounterThreadRunFunction, this);

    SWSS_LOG_INFO("Flex Counter thread started");
}

void FlexCounter::endFlexCounterThread(void)
{
    SWSS_LOG_ENTER();

    MUTEX;

    if (!m_runFlexCounterThread)
    {
        return;
    }

    m_runFlexCounterThread = false;

    notifyPoll();

    m_cvSleep.notify_all();

    if (m_flexCounterThread != nullptr)
    {
        auto fcThread = std::move(m_flexCounterThread);

        MUTEX_UNLOCK; // NOTE: explicit unlock before join to not cause deadlock

        SWSS_LOG_INFO("Wait for Flex Counter thread to end");

        fcThread->join();
    }

    SWSS_LOG_INFO("Flex Counter thread ended");
}

sai_status_t FlexCounter::querySupportedPortCounters(
        _In_ sai_object_id_t portRid)
{
    SWSS_LOG_ENTER();

    sai_stat_capability_list_t stats_capability;
    stats_capability.count = 0;
    stats_capability.list = nullptr;

    /* First call is to check the size needed to allocate */
    sai_status_t status = m_vendorSai->queryStatsCapability(
        portRid,
        SAI_OBJECT_TYPE_PORT,
        &stats_capability);

    /* Second call is for query statistics capability */
    if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        std::vector<sai_stat_capability_t> statCapabilityList(stats_capability.count);
        stats_capability.list = statCapabilityList.data();
        status = m_vendorSai->queryStatsCapability(
            portRid,
            SAI_OBJECT_TYPE_PORT,
            &stats_capability);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Unable to get port supported counters for %s",
                sai_serialize_object_id(portRid).c_str());
        }
        else
        {
            for (auto statCapability: statCapabilityList)
            {
                sai_port_stat_t counter = static_cast<sai_port_stat_t>(statCapability.stat_enum);
                m_supportedPortCounters.insert(counter);
            }
        }
    }
    return status;
}

void FlexCounter::getSupportedPortCounters(
        _In_ sai_object_id_t portRid)
{
    SWSS_LOG_ENTER();

    uint64_t value;

    for (int id = SAI_PORT_STAT_IF_IN_OCTETS; id <= SAI_PORT_STAT_IF_OUT_FABRIC_DATA_UNITS; ++id)
    {
        sai_port_stat_t counter = static_cast<sai_port_stat_t>(id);

        sai_status_t status = m_vendorSai->getStats(SAI_OBJECT_TYPE_PORT, portRid, 1, (sai_stat_id_t *)&counter, &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Counter %s is not supported on port RID %s: %s",
                    sai_serialize_port_stat(counter).c_str(),
                    sai_serialize_object_id(portRid).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        m_supportedPortCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedPortCounters(
        _In_ sai_object_id_t portRid)
{
    SWSS_LOG_ENTER();

    if (m_supportedPortCounters.size())
    {
        return;
    }

    /* Query SAI supported port counters */
    sai_status_t status = querySupportedPortCounters(portRid);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* Fallback to legacy approach */
        getSupportedPortCounters(portRid);
    }
}

std::vector<sai_port_stat_t> FlexCounter::saiCheckSupportedPortDebugCounters(
        _In_ sai_object_id_t portId,
        _In_ const std::vector<sai_port_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    std::vector<sai_port_stat_t> supportedPortDebugCounters;

    uint64_t value;

    for (auto counter: counterIds)
    {
        if (counter < SAI_PORT_STAT_IN_DROP_REASON_RANGE_BASE || counter >= SAI_PORT_STAT_OUT_DROP_REASON_RANGE_END)
        {
            SWSS_LOG_NOTICE("Debug counter %s out of bounds", sai_serialize_port_stat(counter).c_str());
            continue;
        }

        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_PORT,
                portId,
                1,
                (const sai_stat_id_t *)&counter,
                SAI_STATS_MODE_READ,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("Debug counter %s is not supported on port RID %s: %s",
                    sai_serialize_port_stat(counter).c_str(),
                    sai_serialize_object_id(portId).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        supportedPortDebugCounters.push_back(counter);
    }

    return supportedPortDebugCounters;
}

sai_status_t FlexCounter::querySupportedQueueCounters(
        _In_ sai_object_id_t queueId)
{
    SWSS_LOG_ENTER();

    sai_stat_capability_list_t stats_capability;
    stats_capability.count = 0;
    stats_capability.list = nullptr;

    /* First call is to check the size needed to allocate */
    sai_status_t status = m_vendorSai->queryStatsCapability(
        queueId,
        SAI_OBJECT_TYPE_QUEUE,
        &stats_capability);

    /* Second call is for query statistics capability */
    if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        std::vector<sai_stat_capability_t> statCapabilityList(stats_capability.count);
        stats_capability.list = statCapabilityList.data();
        status = m_vendorSai->queryStatsCapability(
            queueId,
            SAI_OBJECT_TYPE_QUEUE,
            &stats_capability);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Unable to get queue supported counters for %s",
                sai_serialize_object_id(queueId).c_str());
        }
        else
        {
            for (auto statCapability: statCapabilityList)
            {
                auto currentStatModes = statCapability.stat_modes;
                if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR && !isStatsModeSupported(currentStatModes, SAI_STATS_MODE_READ_AND_CLEAR))
                {
                    continue;
                }
                sai_queue_stat_t counter = static_cast<sai_queue_stat_t>(statCapability.stat_enum);
                m_supportedQueueCounters.insert(counter);
            }
        }
    }
    return status;
}

void FlexCounter::getSupportedQueueCounters(
        _In_ sai_object_id_t queueId,
        _In_ const std::vector<sai_queue_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    uint64_t value;

    for (auto &counter : counterIds)
    {
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_QUEUE,
                queueId,
                1, (const sai_stat_id_t *)&counter,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("%s: counter %s is not supported on queue %s, rv: %s",
                    m_instanceId.c_str(),
                    sai_serialize_queue_stat(counter).c_str(),
                    sai_serialize_object_id(queueId).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(SAI_OBJECT_TYPE_QUEUE, queueId, 1, (const sai_stat_id_t *)&counter);
            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_NOTICE("%s: clear counter %s is not supported on queue %s, rv: %s",
                        m_instanceId.c_str(),
                        sai_serialize_queue_stat(counter).c_str(),
                        sai_serialize_object_id(queueId).c_str(),
                        sai_serialize_status(status).c_str());

                continue;
            }
        }

        m_supportedQueueCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedQueueCounters(
        _In_ sai_object_id_t queueId,
        _In_ const std::vector<sai_queue_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    m_supportedQueueCounters.clear();

    /* Query SAI supported queue counters */
    sai_status_t status = querySupportedQueueCounters(queueId);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* Fallback to legacy approach */
        getSupportedQueueCounters(queueId, counterIds);
    }
}

sai_status_t FlexCounter::querySupportedPriorityGroupCounters(
        _In_ sai_object_id_t priorityGroupRid)
{
    SWSS_LOG_ENTER();

    sai_stat_capability_list_t stats_capability;
    stats_capability.count = 0;
    stats_capability.list = nullptr;

    /* First call is to check the size needed to allocate */
    sai_status_t status = m_vendorSai->queryStatsCapability(
        priorityGroupRid,
        SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
        &stats_capability);

    /* Second call is for query statistics capability */
    if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        std::vector<sai_stat_capability_t> statCapabilityList(stats_capability.count);
        stats_capability.list = statCapabilityList.data();
        status = m_vendorSai->queryStatsCapability(priorityGroupRid,
            SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
            &stats_capability);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Unable to get priority group supported counters for %s",
                sai_serialize_object_id(priorityGroupRid).c_str());
        }
        else
        {

            for (auto statCapability: statCapabilityList)
            {
                auto currentStatModes = statCapability.stat_modes;
                if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR && !isStatsModeSupported(currentStatModes, SAI_STATS_MODE_READ_AND_CLEAR))
                {
                    continue;
                }
                sai_ingress_priority_group_stat_t counter = static_cast<sai_ingress_priority_group_stat_t>(statCapability.stat_enum);
                m_supportedPriorityGroupCounters.insert(counter);
            }
        }
    }
    return status;
}

void FlexCounter::getSupportedPriorityGroupCounters(
        _In_ sai_object_id_t priorityGroupRid,
        _In_ const std::vector<sai_ingress_priority_group_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    uint64_t value;

    for (auto &counter : counterIds)
    {
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                priorityGroupRid,
                1,
                (const sai_stat_id_t *)&counter,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("%s: counter %s is not supported on PG %s, rv: %s",
                    m_instanceId.c_str(),
                    sai_serialize_ingress_priority_group_stat(counter).c_str(),
                    sai_serialize_object_id(priorityGroupRid).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                    priorityGroupRid,
                    1,
                    (const sai_stat_id_t *)&counter);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_NOTICE("%s: clear counter %s is not supported on PG %s, rv: %s",
                        m_instanceId.c_str(),
                        sai_serialize_ingress_priority_group_stat(counter).c_str(),
                        sai_serialize_object_id(priorityGroupRid).c_str(),
                        sai_serialize_status(status).c_str());

                continue;
            }
        }

        m_supportedPriorityGroupCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedPriorityGroupCounters(
        _In_ sai_object_id_t priorityGroupRid,
        _In_ const std::vector<sai_ingress_priority_group_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    m_supportedPriorityGroupCounters.clear();

    /* Query SAI supported priority group counters */
    sai_status_t status = querySupportedPriorityGroupCounters(priorityGroupRid);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* Fallback to legacy approach */
        getSupportedPriorityGroupCounters(priorityGroupRid, counterIds);
    }
}

sai_status_t FlexCounter::querySupportedRifCounters(
        _In_ sai_object_id_t rifRid)
{
    SWSS_LOG_ENTER();

    sai_stat_capability_list_t stats_capability;
    stats_capability.count = 0;
    stats_capability.list = nullptr;

    /* First call is to check the size needed to allocate */
    sai_status_t status = m_vendorSai->queryStatsCapability(
        rifRid,
        SAI_OBJECT_TYPE_ROUTER_INTERFACE,
        &stats_capability);

    /* Second call is for query statistics capability */
    if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        std::vector<sai_stat_capability_t> statCapabilityList(stats_capability.count);
        stats_capability.list = statCapabilityList.data();
        status = m_vendorSai->queryStatsCapability(
            rifRid,
            SAI_OBJECT_TYPE_ROUTER_INTERFACE,
            &stats_capability);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Unable to get routed port supported counters for %s",
                sai_serialize_object_id(rifRid).c_str());
        }
        else
        {
            for (auto statCapability: statCapabilityList)
            {
                sai_router_interface_stat_t counter = static_cast<sai_router_interface_stat_t>(statCapability.stat_enum);
                m_supportedRifCounters.insert(counter);
            }
        }
    }
    return status;
}

void FlexCounter::getSupportedRifCounters(
        _In_ sai_object_id_t rifRid)
{
    SWSS_LOG_ENTER();

    uint64_t value;

    for (int cntr_id = SAI_ROUTER_INTERFACE_STAT_IN_OCTETS; cntr_id <= SAI_ROUTER_INTERFACE_STAT_OUT_ERROR_PACKETS; ++cntr_id)
    {
        sai_router_interface_stat_t counter = static_cast<sai_router_interface_stat_t>(cntr_id);

        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_ROUTER_INTERFACE,
                rifRid,
                1,
                (const sai_stat_id_t *)&counter,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Counter %s is not supported on router interface RID %s: %s",
                    sai_serialize_router_interface_stat(counter).c_str(),
                    sai_serialize_object_id(rifRid).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        m_supportedRifCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedFlowCounters(
        _In_ sai_object_id_t counterRid,
        _In_ const std::vector<sai_counter_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    if (!m_supportedFlowCounters.empty())
    {
        return;
    }

    uint64_t value;
    for (auto &counter : counterIds)
    {
        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_COUNTER,
                counterRid,
                1,
                (const sai_stat_id_t *)&counter,
                SAI_STATS_MODE_READ,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("%s: counter %s is not supported on flow counter %s, rv: %s",
                    m_instanceId.c_str(),
                    sai_serialize_counter_stat(counter).c_str(),
                    sai_serialize_object_id(counterRid).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        m_supportedFlowCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedRifCounters(
        _In_ sai_object_id_t rifRid)
{
    SWSS_LOG_ENTER();

    if (m_supportedRifCounters.size())
    {
        return;
    }

    /* Query SAI supported rif counters */
    sai_status_t status = querySupportedRifCounters(rifRid);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* Fallback to legacy approach */
        getSupportedRifCounters(rifRid);
    }
}

sai_status_t FlexCounter::querySupportedBufferPoolCounters(
        _In_ sai_object_id_t bufferPoolId,
        _In_ sai_stats_mode_t statsMode)
{
    SWSS_LOG_ENTER();

    sai_stat_capability_list_t stats_capability;
    stats_capability.count = 0;
    stats_capability.list = nullptr;

    /* First call is to check the size needed to allocate */
    sai_status_t status = m_vendorSai->queryStatsCapability(
        bufferPoolId,
        SAI_OBJECT_TYPE_BUFFER_POOL,
        &stats_capability);

    /* Second call is for query statistics capability */
    if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        std::vector<sai_stat_capability_t> statCapabilityList(stats_capability.count);
        stats_capability.list = statCapabilityList.data();
        status = m_vendorSai->queryStatsCapability(
            bufferPoolId,
            SAI_OBJECT_TYPE_BUFFER_POOL,
            &stats_capability);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Unable to get buffer pool supported counters for %s",
                sai_serialize_object_id(bufferPoolId).c_str());
        }
        else
        {
            for (auto statCapability: statCapabilityList)
            {
                auto currentStatModes = statCapability.stat_modes;
                if ((m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR || statsMode == SAI_STATS_MODE_READ_AND_CLEAR) &&
                    !isStatsModeSupported(currentStatModes, SAI_STATS_MODE_READ_AND_CLEAR))
                {
                    continue;
                }
                sai_buffer_pool_stat_t counter = static_cast<sai_buffer_pool_stat_t>(statCapability.stat_enum);
                m_supportedBufferPoolCounters.insert(counter);
            }
        }
    }
    return status;
}

void FlexCounter::getSupportedBufferPoolCounters(
        _In_ sai_object_id_t bufferPoolId,
        _In_ const std::vector<sai_buffer_pool_stat_t> &counterIds,
        _In_ sai_stats_mode_t statsMode)
{
    SWSS_LOG_ENTER();

    uint64_t value;

    for (const auto &counterId : counterIds)
    {
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_BUFFER_POOL,
                bufferPoolId,
                1,
                (const sai_stat_id_t *)&counterId,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("%s: counter %s is not supported on buffer pool %s, rv: %s",
                    m_instanceId.c_str(),
                    sai_serialize_buffer_pool_stat(counterId).c_str(),
                    sai_serialize_object_id(bufferPoolId).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        if (m_statsMode == SAI_STATS_MODE_READ_AND_CLEAR || statsMode == SAI_STATS_MODE_READ_AND_CLEAR)
        {
            status = m_vendorSai->clearStats(
                    SAI_OBJECT_TYPE_BUFFER_POOL,
                    bufferPoolId,
                    1,
                    (const sai_stat_id_t *)&counterId);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("%s: clear counter %s is not supported on buffer pool %s, rv: %s",
                        m_instanceId.c_str(),
                        sai_serialize_buffer_pool_stat(counterId).c_str(),
                        sai_serialize_object_id(bufferPoolId).c_str(),
                        sai_serialize_status(status).c_str());

                continue;
            }
        }

        m_supportedBufferPoolCounters.insert(counterId);
    }
}

void FlexCounter::updateSupportedTunnelCounters(
        _In_ sai_object_id_t tunnelRid,
        _In_ const std::vector<sai_tunnel_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    if (m_supportedTunnelCounters.size())
    {
        return;
    }

    uint64_t value;
    for (auto &counter: counterIds)
    {
        sai_status_t status = m_vendorSai->getStats(
                SAI_OBJECT_TYPE_TUNNEL,
                tunnelRid,
                1,
                (const sai_stat_id_t *)&counter,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_INFO("Counter %s is not supported on tunnel RID %s: %s",
                    sai_serialize_tunnel_stat(counter).c_str(),
                    sai_serialize_object_id(tunnelRid).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        m_supportedTunnelCounters.insert(counter);
    }
}

void FlexCounter::updateSupportedBufferPoolCounters(
        _In_ sai_object_id_t bufferPoolId,
        _In_ const std::vector<sai_buffer_pool_stat_t> &counterIds,
        _In_ sai_stats_mode_t statsMode)
{
    SWSS_LOG_ENTER();

    m_supportedBufferPoolCounters.clear();

    /* Query SAI supported buffer pool counters */
    sai_status_t status = querySupportedBufferPoolCounters(bufferPoolId, statsMode);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* Fallback to legacy approach */
        getSupportedBufferPoolCounters(bufferPoolId, counterIds, statsMode);
    }
}

std::vector<sai_switch_stat_t> FlexCounter::saiCheckSupportedSwitchDebugCounters(
        _In_ sai_object_id_t switchId,
        _In_ const std::vector<sai_switch_stat_t> &counterIds)
{
    SWSS_LOG_ENTER();

    std::vector<sai_switch_stat_t> supportedSwitchDebugCounters;
    uint64_t value;
    for (auto counter: counterIds)
    {
        if (counter < SAI_SWITCH_STAT_IN_DROP_REASON_RANGE_BASE || counter >= SAI_SWITCH_STAT_OUT_DROP_REASON_RANGE_END)
        {
            SWSS_LOG_NOTICE("Debug counter %s out of bounds", sai_serialize_switch_stat(counter).c_str());
            continue;
        }

        sai_status_t status = m_vendorSai->getStatsExt(
                SAI_OBJECT_TYPE_SWITCH,
                switchId,
                1, (const sai_stat_id_t *)&counter,
                SAI_STATS_MODE_READ,
                &value);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("Debug counter %s is not supported on switch RID %s: %s",
                    sai_serialize_switch_stat(counter).c_str(),
                    sai_serialize_object_id(switchId).c_str(),
                    sai_serialize_status(status).c_str());

            continue;
        }

        supportedSwitchDebugCounters.push_back(counter);
    }

    return supportedSwitchDebugCounters;
}

void FlexCounter::removeCounter(
        _In_ sai_object_id_t vid)
{
    MUTEX;

    SWSS_LOG_ENTER();

    auto objectType = VidManager::objectTypeQuery(vid);

    if (objectType == SAI_OBJECT_TYPE_PORT)
    {
        removePort(vid);
        removePortDebugCounters(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_QUEUE)
    {
        removeQueue(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP)
    {
        removePriorityGroup(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_ROUTER_INTERFACE)
    {
        removeRif(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_BUFFER_POOL)
    {
        removeBufferPool(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_SWITCH)
    {
        removeSwitchDebugCounters(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_MACSEC_FLOW)
    {
        removeMACsecFlow(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_MACSEC_SA)
    {
        removeMACsecSA(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_ACL_COUNTER)
    {
        removeAclCounter(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_TUNNEL)
    {
        removeTunnel(vid);
    }
    else if (objectType == SAI_OBJECT_TYPE_COUNTER)
    {
        removeFlowCounter(vid);
    }
    else
    {
        SWSS_LOG_ERROR("Object type for removal not supported, %s",
                sai_serialize_object_type(objectType).c_str());
    }
}

void FlexCounter::addCounter(
        _In_ sai_object_id_t vid,
        _In_ sai_object_id_t rid,
        _In_ const std::vector<swss::FieldValueTuple>& values)
{
    MUTEX;

    SWSS_LOG_ENTER();

    sai_object_type_t objectType = VidManager::objectTypeQuery(vid); // VID and RID will have the same object type

    std::vector<std::string> counterIds;

    std::string statsMode;

    for (const auto& valuePair: values)
    {
        const auto field = fvField(valuePair);
        const auto value = fvValue(valuePair);

        auto idStrings = swss::tokenize(value, ',');

        if (objectType == SAI_OBJECT_TYPE_PORT && field == PORT_COUNTER_ID_LIST)
        {
            std::vector<sai_port_stat_t> portCounterIds;

            for (const auto &str : idStrings)
            {
                sai_port_stat_t stat;
                sai_deserialize_port_stat(str.c_str(), &stat);
                portCounterIds.push_back(stat);
            }

            setPortCounterList(vid, rid, portCounterIds);

        }
        else if (objectType == SAI_OBJECT_TYPE_PORT && field == PORT_DEBUG_COUNTER_ID_LIST)
        {
            std::vector<sai_port_stat_t> portDebugCounterIds;

            for (const auto &str : idStrings)
            {
                sai_port_stat_t stat;
                sai_deserialize_port_stat(str.c_str(), &stat);
                portDebugCounterIds.push_back(stat);
            }

            setPortDebugCounterList(vid, rid, portDebugCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_QUEUE && field == QUEUE_COUNTER_ID_LIST)
        {
            std::vector<sai_queue_stat_t> queueCounterIds;

            for (const auto &str : idStrings)
            {
                sai_queue_stat_t stat;
                sai_deserialize_queue_stat(str.c_str(), &stat);
                queueCounterIds.push_back(stat);
            }

            setQueueCounterList(vid, rid, queueCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_QUEUE && field == QUEUE_ATTR_ID_LIST)
        {
            std::vector<sai_queue_attr_t> queueAttrIds;

            for (const auto &str : idStrings)
            {
                sai_queue_attr_t attr;
                sai_deserialize_queue_attr(str, attr);
                queueAttrIds.push_back(attr);
            }

            setQueueAttrList(vid, rid, queueAttrIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP && field == PG_COUNTER_ID_LIST)
        {
            std::vector<sai_ingress_priority_group_stat_t> pgCounterIds;

            for (const auto &str : idStrings)
            {
                sai_ingress_priority_group_stat_t stat;
                sai_deserialize_ingress_priority_group_stat(str.c_str(), &stat);
                pgCounterIds.push_back(stat);
            }

            setPriorityGroupCounterList(vid, rid, pgCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP && field == PG_ATTR_ID_LIST)
        {
            std::vector<sai_ingress_priority_group_attr_t> pgAttrIds;

            for (const auto &str : idStrings)
            {
                sai_ingress_priority_group_attr_t attr;
                sai_deserialize_ingress_priority_group_attr(str, attr);
                pgAttrIds.push_back(attr);
            }

            setPriorityGroupAttrList(vid, rid, pgAttrIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_ROUTER_INTERFACE && field == RIF_COUNTER_ID_LIST)
        {
            std::vector<sai_router_interface_stat_t> rifCounterIds;

            for (const auto &str : idStrings)
            {
                sai_router_interface_stat_t stat;
                sai_deserialize_router_interface_stat(str.c_str(), &stat);
                rifCounterIds.push_back(stat);
            }

            setRifCounterList(vid, rid, rifCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_SWITCH && field == SWITCH_DEBUG_COUNTER_ID_LIST)
        {
            std::vector<sai_switch_stat_t> switchCounterIds;

            for (const auto &str : idStrings)
            {
                sai_switch_stat_t stat;
                sai_deserialize_switch_stat(str.c_str(), &stat);
                switchCounterIds.push_back(stat);
            }

            setSwitchDebugCounterList(vid, rid, switchCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_MACSEC_FLOW && field == MACSEC_FLOW_COUNTER_ID_LIST)
        {
            std::vector<sai_macsec_flow_stat_t> macsecFlowCounterIds;

            for (const auto &str : idStrings)
            {
                sai_macsec_flow_stat_t stat;
                sai_deserialize_macsec_flow_stat(str.c_str(), &stat);
                macsecFlowCounterIds.push_back(stat);
            }

            setMACsecFlowCounterList(vid, rid, macsecFlowCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_MACSEC_SA && field == MACSEC_SA_COUNTER_ID_LIST)
        {
            std::vector<sai_macsec_sa_stat_t> macsecSACounterIds;

            for (const auto &str : idStrings)
            {
                sai_macsec_sa_stat_t stat;
                sai_deserialize_macsec_sa_stat(str.c_str(), &stat);
                macsecSACounterIds.push_back(stat);
            }

            setMACsecSACounterList(vid, rid, macsecSACounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_MACSEC_SA && field == MACSEC_SA_ATTR_ID_LIST)
        {
            std::vector<sai_macsec_sa_attr_t> macsecSAAttrIds;

            for (const auto &str : idStrings)
            {
                sai_macsec_sa_attr_t attr;
                sai_deserialize_macsec_sa_attr(str, attr);
                macsecSAAttrIds.push_back(attr);
            }

            setMACsecSAAttrList(vid, rid, macsecSAAttrIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_ACL_COUNTER && field == ACL_COUNTER_ATTR_ID_LIST)
        {
            std::vector<sai_acl_counter_attr_t> aclCounterIds;

            for (const auto &str : idStrings)
            {
                sai_acl_counter_attr_t attr{};
                sai_deserialize_acl_counter_attr(str, attr);
                aclCounterIds.push_back(attr);
            }

            setAclCounterAttrList(vid, rid, aclCounterIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_COUNTER && field == FLOW_COUNTER_ID_LIST)
        {
            std::vector<sai_counter_stat_t> counterStatIds;

            for (const auto &str : idStrings)
            {
                sai_counter_stat_t stat;
                sai_deserialize_counter_stat(str.c_str(), &stat);
                counterStatIds.push_back(stat);
            }

            setFlowCounterList(vid, rid, counterStatIds);
        }
        else if (objectType == SAI_OBJECT_TYPE_BUFFER_POOL && field == BUFFER_POOL_COUNTER_ID_LIST)
        {
            counterIds = idStrings;
        }
        else if (objectType == SAI_OBJECT_TYPE_BUFFER_POOL && field == STATS_MODE_FIELD)
        {
            statsMode = value;
        }
        else if (objectType == SAI_OBJECT_TYPE_TUNNEL && field == TUNNEL_COUNTER_ID_LIST)
        {
            std::vector<sai_tunnel_stat_t> tunnelCounterIds;

            for (const auto &str : idStrings)
            {
                sai_tunnel_stat_t stat;
                sai_deserialize_tunnel_stat(str.c_str(), &stat);
                tunnelCounterIds.push_back(stat);
            }

            setTunnelCounterList(vid, rid, tunnelCounterIds);
        }
        else
        {
            SWSS_LOG_ERROR("Object type and field combination is not supported, object type %s, field %s",
                    sai_serialize_object_type(objectType).c_str(),
                    field.c_str());
        }
    }

    // outside loop since required 2 fields BUFFER_POOL_COUNTER_ID_LIST and STATS_MODE_FIELD

    if (objectType == SAI_OBJECT_TYPE_BUFFER_POOL && counterIds.size())
    {
        std::vector<sai_buffer_pool_stat_t> bufferPoolCounterIds;

        for (const auto &str : counterIds)
        {
            sai_buffer_pool_stat_t stat;
            sai_deserialize_buffer_pool_stat(str.c_str(), &stat);
            bufferPoolCounterIds.push_back(stat);
        }

        setBufferPoolCounterList(vid, rid, bufferPoolCounterIds, statsMode);
    }

    // notify thread to start polling
    notifyPoll();
}

void FlexCounter::waitPoll()
{
    SWSS_LOG_ENTER();
    std::unique_lock<std::mutex> lk(m_mtxSleep);
    m_pollCond.wait(lk, [&](){return m_readyToPoll;});
    m_readyToPoll = false;
}

void FlexCounter::notifyPoll()
{
    SWSS_LOG_ENTER();
    std::unique_lock<std::mutex> lk(m_mtxSleep);
    m_readyToPoll = true;
    m_pollCond.notify_all();
}
