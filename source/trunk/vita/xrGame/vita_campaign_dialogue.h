#pragma once
#include <string>
#include <vector>
#include <cstring>

inline bool read_dialog_line(const std::string& xml, const char* key, std::string& line, size_t max_bytes = 512)
{
	const size_t entry = xml.find(std::string("id=\"") + key + "\"");
	const size_t next_entry = xml.find("</string>", entry);
	const size_t text = xml.find("<text>", entry);
	const size_t end = xml.find("</text>", text);
	if (entry == std::string::npos || next_entry == std::string::npos ||
		text == std::string::npos || end == std::string::npos || end > next_entry ||
		end - (text + 6) > max_bytes)
		return false;
	line = xml.substr(text + 6, end - text - 6);
	if (line.empty())
		return false;
	const char* entities[] = {"&apos;", "&quot;", "&lt;", "&gt;", "&amp;"};
	const char* unescaped[] = {"'", "\"", "<", ">", "&"};
	for (size_t j = 0; j < 5; ++j)
	{
		size_t offset = 0;
		while ((offset = line.find(entities[j], offset)) != std::string::npos)
			line.replace(offset++, strlen(entities[j]), unescaped[j]);
	}
	return true;
}

// Campaign replies are longer than the introductory dialogue; paginate after reading them.
inline std::vector<std::string> read_vita_campaign_pages(const std::string& xml,unsigned zone,bool cleared)
{
 if(zone>2)return {};
 std::vector<const char*> keys;
 if(!zone)keys={"escape_trader_jobs_6","escape_trader_jobs_611","escape_trader_jobs_61111","escape_trader_jobs_611111111"};
 else if(zone==1)keys=cleared?std::vector<const char*>{"gar_seriy_start_22","gar_seriy_start_222","gar_seriy_start_2221"}:
  std::vector<const char*>{"gar_seriy_start_11"};
 else keys=cleared?std::vector<const char*>{"agr_factory_krot_start_0","agr_factory_krot_start_2","agr_factory_krot_start_1501","agr_factory_krot_start_3001"}:
  std::vector<const char*>{"agr_krot_actor_hasnot_met_seriy_100"};
 std::vector<std::string> pages;
 for(const char*key:keys){
  std::string text;if(!read_dialog_line(xml,key,text,8192))return {};
  while(text.size()>600){
   size_t split=text.rfind(' ',600);if(split==std::string::npos||split<200)split=600;
   pages.push_back(text.substr(0,split));text.erase(0,split+(text[split]==' '?1:0));
  }
  if(!text.empty())pages.push_back(text);
 }
 return pages;
}

inline std::vector<std::string> read_vita_road_pages(const std::string& xml){
 std::vector<std::string> pages;
 for(const char*key:{"gar_dramm_novice_start_0","gar_dramm_novice_start_31"}){
  std::string text;if(!read_dialog_line(xml,key,text,8192))return {};
  while(text.size()>600){size_t split=text.rfind(' ',600);if(split==std::string::npos||split<200)split=600;
   pages.push_back(text.substr(0,split));text.erase(0,split+(text[split]==' '?1:0));}
  if(!text.empty())pages.push_back(text);
 }return pages;
}
