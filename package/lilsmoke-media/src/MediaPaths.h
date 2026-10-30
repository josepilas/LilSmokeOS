#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace lilsmoke::media {
enum class Kind { Unknown, Audio, Video, Photo, Playlist };
inline Kind kind(const std::string &path) {
    std::string e=std::filesystem::path(path).extension().string();
    std::transform(e.begin(),e.end(),e.begin(),[](unsigned char c){return char(std::tolower(c));});
    static const std::vector<std::string> audio={".mp3",".mp2",".aac",".m4a",".flac",".ogg",".oga",".opus",".wav",".wave",".wma",".aiff",".aif",".ape",".wv",".alac",".ac3",".dts",".au",".amr",".caf",".mka",".adts",".mpa",".a52",".eac3",".spx",".sbc",".tak",".tta",".dsf",".dff"};
    static const std::vector<std::string> video={".mp4",".m4v",".mkv",".webm",".avi",".mov",".mpg",".mpeg",".mpe",".ts",".m2ts",".mts",".vob",".ogv",".flv",".f4v",".wmv",".asf",".3gp",".3g2",".rm",".rmvb",".nut",".bik",".bk2",".smk",".dv",".h264",".h265",".hevc",".m2v",".y4m",".ivf",".mjpeg",".mjpg",".fli",".flc",".roq"};
    static const std::vector<std::string> photo={".png",".jpg",".jpeg",".webp",".bmp",".gif",".tif",".tiff",".tga",".pcx",".pnm",".ppm",".pgm",".pbm",".ico",".qoi",".dds",".exr",".hdr",".avif",".heic",".heif",".j2k",".jp2",".jxl",".psd",".xbm",".xpm",".dpx",".pam",".pfm"};
    if(e==".m3u"||e==".m3u8"||e==".pls")return Kind::Playlist;
    if(std::find(audio.begin(),audio.end(),e)!=audio.end())return Kind::Audio;
    if(std::find(video.begin(),video.end(),e)!=video.end())return Kind::Video;
    if(std::find(photo.begin(),photo.end(),e)!=photo.end())return Kind::Photo;
    return Kind::Unknown;
}
inline bool localFile(const std::filesystem::path &p) {
    std::error_code ec;return p.string().find("://")==std::string::npos&&std::filesystem::is_regular_file(p,ec);
}
inline std::vector<std::string> collection(const std::string &selected) {
    namespace fs=std::filesystem; std::vector<std::string> list;
    const fs::path file=fs::absolute(selected); const Kind type=kind(selected);
    if(type==Kind::Playlist) {
        std::error_code sizeError;const auto size=fs::file_size(file,sizeError);
        if(sizeError||size>262144)return list;
        std::ifstream input(file);std::string line;size_t bytes=0;
        while(std::getline(input,line)&&list.size()<4096&&(bytes+=line.size()+1)<=262144) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.rfind("\xef\xbb\xbf",0)==0)line.erase(0,3);
            if(line.empty()||line[0]=='#'||line[0]=='[')continue;
            std::string extension=file.extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(extension==".pls") {
                std::string key=line.substr(0,4);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
                if(key!="file")continue;
                const auto at=line.find('=');if(at==std::string::npos)continue;line=line.substr(at+1);
            }
            if(line.size()>4096||line.find("://")!=std::string::npos)continue;
            fs::path candidate=line;if(candidate.is_relative())candidate=file.parent_path()/candidate;
            const Kind k=kind(candidate.string());
            if((k==Kind::Audio||k==Kind::Video)&&localFile(candidate))list.push_back(candidate.lexically_normal().string());
        }
        return list;
    }
    std::error_code ec;
    fs::directory_iterator it(file.parent_path(),fs::directory_options::skip_permission_denied,ec),end;
    for(;!ec&&it!=end&&list.size()<4096;it.increment(ec)) {
        const auto path=it->path();if(kind(path.string())==type&&localFile(path))list.push_back(path.string());
    }
    std::sort(list.begin(),list.end());
    if(std::find(list.begin(),list.end(),file.string())==list.end()&&localFile(file))list.insert(list.begin(),file.string());
    return list;
}
class Playlist {
public:
    std::vector<std::string> files;
    size_t index=0;
    bool shuffle=false;
    unsigned repeat=0;
    std::vector<size_t> history;
    std::mt19937 random{std::random_device{}()};
    std::vector<size_t> remaining;
    bool previousShuffle=false;
    bool advance(bool backwards,bool automatic=false) {
        if(files.empty())return false;
        if(automatic&&repeat==2)return true;
        if(backwards) {
            if(!history.empty()){index=history.back();history.pop_back();return true;}
            if(index){--index;return true;}
            index=files.size()-1;return true;
        }
        if(automatic&&!shuffle&&index+1==files.size()&&!repeat)return false;
        if(automatic&&files.size()==1&&!repeat)return false;
        if(shuffle!=previousShuffle){remaining.clear();previousShuffle=shuffle;shuffleCycleStarted_=false;}
        if(shuffle&&files.size()>1&&remaining.empty()) {
            if(automatic&&shuffleCycleStarted_&&!repeat)return false;
            for(size_t i=0;i<files.size();++i)if(i!=index)remaining.push_back(i);
            std::shuffle(remaining.begin(),remaining.end(),random);shuffleCycleStarted_=true;
        }
        history.push_back(index);if(history.size()>4096)history.erase(history.begin());
        if(shuffle&&files.size()>1) {
            index=remaining.back();remaining.pop_back();
        } else index=(index+1)%files.size();
        return true;
    }
private:
    bool shuffleCycleStarted_=false;
};
}
