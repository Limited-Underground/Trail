#pragma once
namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
// Fixed, optional evaluation observer. No SDK calls or persistence of its own.
struct StartupDiagnosticPort {
    void* context{};
    void (*observe)(void*,unsigned,unsigned){};
    void mark(unsigned stage,unsigned phase) const {if(observe)observe(context,stage,phase);}
};
}
