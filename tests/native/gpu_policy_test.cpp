#include "gpu_policy.h"
#include "native_result_trace.h"
#include <stdexcept>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void runGpuPolicyTests() {
    using P=fusion::GpuCostProfile;
    P phone;const double estimate=P::reusedCost(75.403,48.648)+15.;
    phone.record(true,75.403,48.648,143.245,estimate,143.245,"no missing proposals");
    require(phone.state==P::Gpu,"Conforming warm phone workload was charged permanent cold cost");
    P slow;
    for(int attempt=0;attempt<3;++attempt) {
        require(slow.eligible(),"Bounded retry never became eligible");
        slow.record(true,180,160,100,165,100);
        for(int i=0;i<8;++i)require(!slow.eligible(),"Cost retry ran on every call");
    }
    for(int i=0;i<100;++i)require(!slow.eligible(),"Unbounded calibration attempts");
    P numeric;numeric.record(false,20,10,100,12.5,100);
    for(int i=0;i<100;++i)require(!numeric.eligible(),"Cost retry weakened numerical rejection");
    P independent;require(independent.eligible(),"A failed profile disabled an unrelated workload");
    P nonfinite;nonfinite.record(true,NAN,1,100,1,100);require(!nonfinite.eligible(),"Nonfinite calibration was admitted");
    fusion::NativeResultTrace trace;trace.publish(11,"old capture");trace.publish(22,"new capture");
    require(trace.take(11).empty(),"Old handle received a new capture trace");
    require(trace.take(22)=="new capture"&&trace.take(22).empty(),"Trace was stale or consumed more than once");
    trace.publish(33,std::string(20000,'x'));require(trace.take(33).size()==16384,"Trace bound violated");
    trace.publish(44,"failed result");trace.clear();require(trace.take(44).empty(),"Failed result leaked diagnostics");
    std::cout<<"PASS handset warm-cost decision, bounded retry, numeric rejection, profile and result-trace isolation\n";
}
