#pragma once
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <filesystem>
namespace lilsmoke::media {
struct Caption {double begin=0,end=0;std::string text;};
inline double captionTime(std::string text) {
    std::replace(text.begin(),text.end(),',','.');int h=0,m=0;double s=0;
    if(std::sscanf(text.c_str(),"%d:%d:%lf",&h,&m,&s)==3)return h*3600+m*60+s;
    if(std::sscanf(text.c_str(),"%d:%lf",&m,&s)==2)return m*60+s;
    return -1;
}
inline std::vector<Caption> loadCaptions(const std::string &path) {
    std::error_code error;auto size=std::filesystem::file_size(path,error);
    if(error||size>262144)return {};
    std::ifstream file(path);std::vector<Caption> out;std::string line;Caption current;bool active=false;size_t bytes=0;
    while(std::getline(file,line)&&out.size()<4096&&(bytes+=line.size()+1)<=262144) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto at=line.find("-->");
        if(at!=std::string::npos) {
            if(active&&!current.text.empty())out.push_back(current);
            current={captionTime(line.substr(0,at)),captionTime(line.substr(at+3)),""};active=current.begin>=0&&current.end>=current.begin;
        } else if(line.empty()) {if(active&&!current.text.empty())out.push_back(current);active=false;}
        else if(active&&current.text.size()<512){if(!current.text.empty())current.text+='\n';current.text+=line.substr(0,512-current.text.size());}
    }
    if(active&&!current.text.empty())out.push_back(current);return out;
}
}
