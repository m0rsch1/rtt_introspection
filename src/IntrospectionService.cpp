#include "IntrospectionTypes.hpp"
#include "IntrospectionService.hpp"
#include <rtt/TaskContext.hpp>
#include <rtt/Port.hpp>
#include <rtt/transports/mqueue/MQChannelElement.hpp>
#include <rtt/plugin/ServicePlugin.hpp>
#include <rtt/extras/FileDescriptorActivity.hpp>
#include <rtt/Activity.hpp>
#include <rtt/Time.hpp>
#include <rtt/DataFlowInterface.hpp>
#include <orocos_cpp/PluginHelper.hpp>
#include <orocos_cpp_base/ProxyPort.hpp>
#include <sys/types.h>
#include <unistd.h>

using namespace orocos_cpp;

namespace RTT
{
namespace introspection
{

const std::string IntrospectionService::ServiceName = std::string("Introspection");
const std::string IntrospectionService::OperationName = std::string("getIntrospectionInformation");
    
IntrospectionService::IntrospectionService(RTT::TaskContext* owner): Service(ServiceName, owner)
{
    PluginHelper::loadTypekitAndTransports("rtt_introspection");
    _op = new RTT::Operation<TaskData ()>(OperationName, boost::bind(&IntrospectionService::getIntrospectionInformation, this));
    owner->addOperation(*_op);
    _owner = owner;
}
IntrospectionService::~IntrospectionService()
{
    // Here we have to make sure that the owner does not have _op anymore
    _owner->operations()->remove(OperationName);
}

const char* get_process_name_by_pid(const int pid)
{
    char* name = (char*)calloc(1024,sizeof(char));
    if(name){
        std::sprintf(name, "/proc/%d/cmdline",pid);
        std::FILE* f = std::fopen(name,"r");
        if(f){
            size_t size;
            size = std::fread(name, sizeof(char), 1024, f);
            if(size>0){
                if('\n'==name[size-1])
                    name[size-1]='\0';
            }
            std::fclose(f);
        }
    }
    return name;
}

TaskData IntrospectionService::getIntrospectionInformation()
{
    RTT::TaskContext* task = getOwner();
    
    TaskData taskData;
    taskData.taskName = task->getName();

    // Get the hostname of the running system
    char hostName[256];
    gethostname(hostName, 256);
    taskData.taskHost = std::string(hostName);
    
    taskData.taskDeployment = get_process_name_by_pid(getpid());
    taskData.taskCommand = task->engine()->getThread()->getName();
    taskData.taskPid = getpid();
//     RTT::base::PortInterface* pi = task->getPort("state");
//     OutputPort<int32_t>* stateop = (OutputPort< int32_t >*)pi;
//     stateop->getLastWrittenValue();
    task->properties();
    switch (task->getTaskState())
    {
        case RTT::base::TaskCore::TaskState::PreOperational:
            taskData.taskState = "PRE_OPERATIONAL";
            break;
        case RTT::base::TaskCore::TaskState::Stopped:
            taskData.taskState = "STOPPED";
            break;
        case RTT::base::TaskCore::TaskState::Running:
            taskData.taskState = "RUNNING";
            break;
        default:
            taskData.taskState = "EXCEPTION";
    }
   
    
    base::ActivityInterface* acinterface = task->getActivity();
    
    
    if(RTT::extras::FileDescriptorActivity* fda = dynamic_cast<RTT::extras::FileDescriptorActivity*>(acinterface))
    {
        taskData.taskActivity.type = "FDDRIVEN";
        taskData.taskActivity.realTime = fda->getScheduler() == ORO_SCHED_RT;
        taskData.taskActivity.priority = fda->getPriority();
        // Not able to get File Descriptor yet
    }
    else if(RTT::Activity* acti = dynamic_cast<RTT::Activity*>(acinterface))
    {
        if (acti->isPeriodic())
        {
            taskData.taskActivity.type = "PERIODIC";
        }
        else
        {
            taskData.taskActivity.type = "NONE";
//             RTT::DataFlowInterface* dfi = task->ports();
//             RTT::Service::shared_ptr rttservice = task->provides();
        }
        taskData.taskActivity.period = acti->getPeriod();
        taskData.taskActivity.priority = acti->getPriority();
        taskData.taskActivity.realTime = acti->getScheduler() == ORO_SCHED_RT;
        // Not able to get event Ports
    }
    
    RTT::OperationInterfacePart* opif = task->getOperation("getModelName");
    
    if(opif)
    {
        RTT::OperationCaller<std::string ()> getModelName(opif);
        taskData.taskType = getModelName();
    }
    
    RTT::DataFlowInterface *dfif = task->ports();
    for(RTT::base::PortInterface *port : dfif->getPorts())
    {
        PortData portData;
        portData.portName = port->getName();

        
        if(dynamic_cast<RTT::base::InputPortInterface *>(port))
        {
            portData.type = PortData::INPUT;
        }
        else
        {
            portData.type = PortData::OUTPUT;
        }
        
        const RTT::internal::ConnectionManager *conManager = port->getManager();
        if(!conManager)
            throw std::runtime_error("Introspection :Error, no connection manager found");
        
        conManager->lock();
        
        for(const RTT::internal::ConnectionManager::ChannelDescriptor &desc : conManager->getChannels())
        {
            ConnectionData connData;
            connData.policy = boost::get<2>(desc);
            
            RTT::base::ChannelElementBase *elem = boost::get<1>(desc).get();
            int cnt = 0;
            while(elem)
            {
                ElementData elemdata;
                elemdata.type = elem->getElementName();
                if(elemdata.type == "ChannelBufferElement")
                {
                    const RTT::internal::ChannelBufferElementBase *buffer = dynamic_cast<const RTT::internal::ChannelBufferElementBase *>(elem);
                    if(buffer)
                    {
                        elemdata.bufferSize = buffer->getBufferSize();
                        elemdata.droppedSamples = buffer->getNumDroppedSamples();
                        elemdata.numSamples = buffer->getBufferFillSize();
                    }
                    else
                    {
                        conManager->unlock();

                        throw std::runtime_error("Introspection : Error could not cast to ChannelBufferElementBase but type is ChannelBufferElement");
                    }
                }

                elemdata.remoteElement = elem->isRemoteElement();
                elemdata.localURI = elem->getLocalURI();
                elemdata.remoteURI = elem->getRemoteURI();
                
                connData.elementData.push_back(elemdata);

                if(portData.type == PortData::INPUT)
                {
                    //traverse the chain
                    elem = elem->getInput().get();
                }
                else
                {
                    //traverse the chain
                    elem = elem->getOutput().get();
                }

                cnt++;
            }
            
            portData.connectionData.push_back(connData);
        }
        conManager->unlock();

        taskData.portData.push_back(portData);
    }
    
    return taskData;
}

}
}

ORO_SERVICE_NAMED_PLUGIN(RTT::introspection::IntrospectionService, RTT::introspection::IntrospectionService::ServiceName)
