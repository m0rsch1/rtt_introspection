#ifndef INTROSPECTIONSERVICE_H
#define INTROSPECTIONSERVICE_H

#include <rtt/Service.hpp>
#include "IntrospectionTypes.hpp"

namespace RTT
{
namespace introspection
{
    
class IntrospectionService : public RTT::Service
{
public:
    static const std::string ServiceName;
    static const std::string OperationName;
    IntrospectionService(RTT::TaskContext* owner = 0);
    ~IntrospectionService();
    
private:
    RTT::Operation<TaskData ()> *_op;
    RTT::TaskContext* _owner;
    TaskData getIntrospectionInformation();
};

}
}

#endif // INTROSPECTIONSERVICE_H

