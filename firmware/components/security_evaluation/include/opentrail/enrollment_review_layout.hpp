#pragma once
// Complete, bounded text for the existing 128x64, 6x8-cell panel geometry.
// Rendering is not confirmation or identity authority. No names/aliases or
// clipping may substitute for the full fingerprint, group, role and purpose.
#include "opentrail/enrollment_fingerprint_review.hpp"
#include <string_view>
#include "opentrail/enrollment_review_display.hpp"

namespace opentrail::security_evaluation {
inline bool enrollment_review_layout(const FingerprintReviewFrame& frame,EnrollmentReviewLayout& output) {
    if(!frame.revision || !frame.group ||
       (frame.local_role!=InvitationRole::initiator && frame.local_role!=InvitationRole::responder))return false;
    const bool identity=frame.purpose==EnrollmentDisplayPurpose::identity_review;
    if(!identity && frame.purpose!=EnrollmentDisplayPurpose::transcript_confirmation)return false;
    FingerprintReviewFrame canonical{};
    if(!identity){canonical.domain={};constexpr char code[]="OT-CODE1";std::memcpy(canonical.domain.data(),code,sizeof(code));}
    if(frame.domain!=canonical.domain || (!identity && !frame.peer_page))return false;
    const auto hex=[](char c){return (c>='0' && c<='9') || (c>='A' && c<='F');};
    for(std::size_t r=0;r<4;++r)for(std::size_t c=0;c<17;++c){
        const bool digit=identity ? c<16 : r==0 && c<8;
        if(digit ? !hex(frame.digits[r][c]) : frame.digits[r][c]!=0)return false;
    }
    EnrollmentReviewLayout staged{};
    const auto row=[&](unsigned r,std::string_view text){
        if(text.size()>EnrollmentReviewLayout::columns)return false;
        std::memcpy(staged.text[r].data(),text.data(),text.size());return true;
    };
    const bool inviter=frame.local_role==InvitationRole::initiator;
    if(!row(0,identity ? (frame.peer_page ? "PEER ID" : "MY ID") : "COMPARE CODE"))return false;
    const auto role=inviter ? "INVITER" : "MEMBER";
    const auto offset=EnrollmentReviewLayout::columns-std::strlen(role);
    const auto title_size=std::strlen(staged.text[0].data());
    for(auto i=title_size;i<offset;++i)staged.text[0][i]=' ';
    std::memcpy(staged.text[0].data()+offset,role,std::strlen(role));
    if(!row(1,std::string_view(frame.domain.data(),std::strlen(frame.domain.data()))))return false;
    if(identity){for(unsigned r=0;r<4;++r)if(!row(r+2,{frame.digits[r].data(),16}))return false;}
    else if(!row(3,{frame.digits[0].data(),8}))return false;
    staged.text[6][0]='G';staged.text[6][1]=':';
    constexpr char digits[]="0123456789ABCDEF";
    for(unsigned i=0;i<16;++i)staged.text[6][i+2]=digits[(frame.group>>(60-i*4))&15];
    if(!row(7,identity && !frame.peer_page ? "SHOW PEER TO CONFIRM" : "HOLD 1S THEN RELEASE"))return false;
    output=staged;return true;
}
}
