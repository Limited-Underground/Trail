#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_review_layout.hpp"
using namespace product_enrollment_test;
int main(){
 unsigned groups=0;
 ProductPair pair;
 const auto check=[&](const FingerprintReviewFrame& f){
  EnrollmentReviewLayout out{};CHECK(enrollment_review_layout(f,out));
  for(const auto& row:out.text)CHECK(std::strlen(row.data())<=21 && row[21]==0);
  CHECK(std::string_view(out.text[6].data())=="G:0000000000000011");
  CHECK(std::string_view(out.text[7].data())=="HOLD 1S THEN RELEASE");
 };
 for(auto* n:{&pair.a,&pair.b}){
  check(n->port.frame);EnrollmentReviewLayout out{};CHECK(enrollment_review_layout(n->port.frame,out));
  for(unsigned r=0;r<4;++r)CHECK(std::string_view(out.text[r+2].data())==std::string_view(n->port.frame.digits[r].data(),16));
  CHECK(std::string_view(out.text[0].data()).find(n==&pair.a ? "INVITER" : "MEMBER")!=std::string_view::npos);++groups;
 }
 const auto identity=pair.a.port.frame;
 pair.handshake();CHECK(pair.a.endpoint->poll_confirmation());const auto code=pair.a.port.frame;
 check(code);++groups;
 for(const auto& original:{identity,code})for(unsigned variant=0;variant<8;++variant){
  auto f=original;
  switch(variant){case 0:f.group=0;break;case 1:f.revision=0;break;case 2:f.local_role=static_cast<InvitationRole>(0);break;
   case 3:f.domain[0]='X';break;case 4:f.digits[0][0]='g';break;case 5:f.digits[0][0]=0;break;
   case 6:f.digits[0][16]='X';break;case 7:f.purpose=static_cast<EnrollmentDisplayPurpose>(99);break;}
  EnrollmentReviewLayout out{};out.text[0][0]='Z';const auto before=out.text;
  CHECK(!enrollment_review_layout(f,out) && out.text==before);++groups;
 }
 {auto f=identity;f.peer_page=false;f.group=UINT64_MAX;EnrollmentReviewLayout out{};
  CHECK(enrollment_review_layout(f,out));CHECK(std::string_view(out.text[6].data())=="G:FFFFFFFFFFFFFFFF");
  CHECK(std::string_view(out.text[7].data())=="SHOW PEER TO CONFIRM");++groups;}
 {auto f=code;f.digits[1][0]='1';EnrollmentReviewLayout out{};CHECK(!enrollment_review_layout(f,out));++groups;}
 std::cout<<"PASS "<<groups<<" enrollment review layout groups\n";
}
