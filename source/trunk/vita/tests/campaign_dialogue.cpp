#include "vita_campaign_dialogue.h"
#include "vita_inventory.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char**argv)
{
 assert(argc==2);
 const char* zones[]={"escape","garbage","agroprom"};
 for(const char*language:{"fra","eng"})
  for(unsigned zone=0;zone<3;++zone){
   std::ifstream file(std::string(argv[1])+"/config/text/"+language+"/stable_dialogs_"+zones[zone]+".xml",std::ios::binary);
   assert(file);
   const std::string xml((std::istreambuf_iterator<char>(file)),{});
   for(bool cleared:{false,true}){
    auto pages=read_vita_campaign_pages(xml,zone,cleared);
    assert(!pages.empty());
    for(const auto&page:pages)assert(!page.empty()&&page.size()<=600);
    std::cout<<language<<" zone="<<zone<<" cleared="<<cleared<<" pages="<<pages.size()<<"\n";
   }
   if(zone==1)assert(!read_vita_road_pages(xml).empty());
   if(!zone){
    std::string line;
    // Reproduce the old 512-byte rejection on the real post-USB replies.
    assert(!read_dialog_line(xml,"escape_trader_jobs_611",line));
    assert(read_dialog_line(xml,"escape_trader_jobs_611",line,8192));
    auto pages=read_vita_campaign_pages(xml,0,true);
    std::string joined;for(const auto&p:pages){if(!joined.empty())joined+=' ';joined+=p;}
    assert(joined.find(line)!=std::string::npos);
    assert(read_dialog_line(xml,"escape_trader_jobs_61111",line,8192));
    assert(joined.find(line)!=std::string::npos);
   }
  }
 assert(read_vita_campaign_pages("",0,true).empty());
 assert(read_vita_campaign_pages("<string id=\"escape_trader_jobs_6\"><text>broken",0,true).empty());
 assert(read_vita_campaign_pages("",3,true).empty());
 std::string line;
 assert(!read_dialog_line("<string id=\"long\"><text>"+std::string(8193,'x')+"</text></string>","long",line,8192));
 assert(read_dialog_line("<string id=\"entities\"><text>&quot;A&amp;B&quot;</text></string>","entities",line)&&line=="\"A&B\"");
 VitaInventory inventory;
 assert(inventory.grant_starting_equipment());
 assert(inventory.receive_flash());
 assert(inventory.deliver_flash());
 const auto reward=inventory.state().cash;
 assert(inventory.flash_status()==VitaFlashStatus::Delivered);
 assert(!inventory.deliver_flash()&&inventory.state().cash==reward);
 std::cout<<"Post-USB replies, pagination, malformed input, and single reward passed.\n";
}
