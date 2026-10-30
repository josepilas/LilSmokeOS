// SPDX-License-Identifier: MPL-2.0
#include "course_objects.hpp"
#include "extraction.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace portsmoke::sm64 {
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
unsigned half(const std::vector<std::uint8_t>& b, std::size_t at) {
    check(at < b.size() && b.size()-at >= 2, "truncated macro object list");
    return unsigned(b[at])*256+b[at+1];
}
float signedHalf(const std::vector<std::uint8_t>& b, std::size_t at) {
    unsigned n=half(b,at); return n<32768 ? int(n) : int(n)-65536;
}
void add(CourseState& course, ObjectKind kind, Position position, unsigned bit=0) {
    check(course.objects.size()<128, "course object budget exceeded");
    course.objects.push_back({kind,position,position,0,0,bit,true});
}
std::vector<std::uint8_t> wrapper(unsigned root, unsigned light=0) {
    std::vector<std::uint8_t> out;
    auto word=[&](unsigned n){for(unsigned shift:{24u,16u,8u,0u})out.push_back(std::uint8_t(n>>shift));};
    auto cmd=[&](unsigned a,unsigned b){word(a);word(b);};
    cmd(0xb6000000,0xffffffff);cmd(0xb7000000,0x22205);cmd(0xbb000000,0xffffffff);
    if(light){cmd(0x03860010,light+8);cmd(0x03880010,light);}
    cmd(0x06000000,root);cmd(0xb8000000,0);
    return out;
}
SceneMesh part(SegmentMemory& memory, unsigned root, unsigned light=0) {
    memory.bind(1,wrapper(root,light));return interpretF3d(memory,{0x01000000});
}
float squared(float n) {return n*n;}
}

CourseState loadBobObjects(const std::filesystem::path& cache, const CollisionWorld& world) {
    RuntimeAssets assets(cache/"segments-us-v1.pdat", allSegmentDefinition());
    auto data=assets.pack().load("segments/mio0-003fc2b0.bin");
    CourseState course;
    // US ROM's original macro list; only supported presets become objects.
    bool ended=false;
    for(std::size_t at=0x1104c, count=0; count<256; at+=10, ++count) {
        const unsigned encoded=half(data,at), preset=encoded&511;
        if(preset<31){ended=true;break;}
        Position p{signedHalf(data,at+2),signedHalf(data,at+4),signedHalf(data,at+6)};
        const unsigned id=preset-31;
        if(id==0||id==1||id==4) {
            p.y+=50;add(course,id==4?ObjectKind::RedCoin:ObjectKind::Coin,p);
        } else if(id==6||id==7) {
            float yaw=(encoded>>9)*3.1415926536f/16;
            unsigned total=id==6?5:8;
            for(unsigned i=0;i<total;++i) {
                float x=id==6?float(int(i)*160-320):300*std::sin(i*6.2831853072f/8);
                float z=id==6?0:300*std::cos(i*6.2831853072f/8);
                Position q{p.x+x*std::cos(yaw)+z*std::sin(yaw),p.y+300,p.z-x*std::sin(yaw)+z*std::cos(yaw)};
                auto floor=world.floor(q.x,q.y,q.z);
                if(floor.surface)q.y=floor.height+50;
                add(course,ObjectKind::Coin,q);
            }
        } else if(id==37||id==32||id==33) {
            unsigned total=id==37?1:id==32?3:5;
            for(unsigned i=0;i<total;++i) {
                Position q=p;
                if(total>1){q.x+=300*std::sin(i*6.2831853072f/total);q.z+=300*std::cos(i*6.2831853072f/total);}
                auto floor=world.floor(q.x,q.y+500,q.z);
                if(floor.surface){q.y=floor.height;add(course,ObjectKind::Goomba,q);}
            }
        }
    }
    check(ended,"unterminated Bob macro list");
    add(course,ObjectKind::Star,{1550,1200,300},1U<<5);
    add(course,ObjectKind::Star,{-6000,1000,2400},1U<<3);
    course.objects.back().active=false;
    return course;
}

ObjectGraphics loadObjectGraphics(const std::filesystem::path& cache) {
    RuntimeAssets assets(cache/"sm64-us-v1.pdat",allAssetDefinition());
    RuntimeAssets segments(cache/"segments-us-v1.pdat",allSegmentDefinition());
    ObjectGraphics out;
    const char* names[]={"coin_front","coin_tilt_right","coin_side","coin_tilt_left"};
    for(unsigned i=0;i<4;++i) {
        auto& mesh=out.coins[i];
        mesh.textures.push_back({0,assets.texture(std::string("actors/coin/")+names[i]+".ia16.png")});
        const SceneVertex vertices[]={
            {-50,-50,0,0,1,{255,255,255,255}}, {50,-50,0,1,1,{255,255,255,255}}, {50,50,0,1,0,{255,255,255,255}},
            {-50,-50,0,0,1,{255,255,255,255}}, {50,50,0,1,0,{255,255,255,255}}, {-50,50,0,0,0,{255,255,255,255}}};
        mesh.vertices.assign(std::begin(vertices),std::end(vertices));mesh.batches.push_back({0,6,0,false});
    }
    SegmentMemory memory;
    memory.bind(3,segments.pack().load("segments/mio0-00201410.bin"));
    memory.bind(8,segments.pack().load("segments/mio0-001f2200.bin"));
    out.star=part(memory,0x0302b7b0,0x0302a6d8);
    for(auto& v:out.star.vertices)v.color={255,218,30,255};
    auto eyes=part(memory,0x0302ba18,0x0302b908);
    unsigned textureOffset=unsigned(out.star.textures.size()), vertexOffset=unsigned(out.star.vertices.size());
    out.star.textures.insert(out.star.textures.end(),eyes.textures.begin(),eyes.textures.end());
    out.star.vertices.insert(out.star.vertices.end(),eyes.vertices.begin(),eyes.vertices.end());
    for(auto batch:eyes.batches){batch.first+=vertexOffset;if(batch.texture>=0)batch.texture+=textureOffset;out.star.batches.push_back(batch);}
    out.goombaHead=part(memory,0x0801b5c8);
    out.goombaBody=part(memory,0x0801b690);
    out.goombaLeftFoot=part(memory,0x0801ce20);
    out.goombaRightFoot=part(memory,0x0801cf78);
    return out;
}

CourseEvents stepObjects(CourseState& course, PlayerState& player, const CollisionWorld& world) {
    CourseEvents events;
    ++course.ticks;if(course.invulnerable)--course.invulnerable;
    for(auto& object:course.objects) {
        if(!object.active)continue;
        ++object.age;
        float dx=player.position.x-object.position.x,dz=player.position.z-object.position.z;
        if(object.kind==ObjectKind::Goomba) {
            const float distance=std::hypot(dx,dz);
            if(distance<1200)object.yaw=std::atan2(dx,dz);
            else if(object.age%90==0)object.yaw+=1.4f;
            Position next=object.position;
            float speed=distance<1200?8.f:3.f;
            next.x+=std::sin(object.yaw)*speed;next.z+=std::cos(object.yaw)*speed;
            auto floor=world.floor(next.x,object.position.y+100,next.z);
            if(floor.surface&&std::abs(floor.height-object.position.y)<70) {
                next.y=floor.height;world.walls(next,40,45);object.position=next;
            } else object.yaw+=3.1415926536f;
            if(distance<110 && player.position.y-object.position.y<150 && player.position.y>=object.position.y-20) {
                if(player.velocityY<0&&player.position.y>object.position.y+40) {
                    object.kind=ObjectKind::Coin;object.position.y+=50;player.velocityY=42;player.grounded=false;
                    events.stomp=true;
                } else if(!course.invulnerable) {
                    course.health=course.health>2?course.health-2:0;course.invulnerable=60;events.hurt=true;
                    player.velocityY=24;player.grounded=false;
                    if(!course.health){resetPlayer(player,world);course.health=8;}
                }
            }
        } else if(squared(dx)+squared(dz)<squared(100) &&
                  std::abs(player.position.y+80-object.position.y)<130) {
            if(object.kind==ObjectKind::Star) {
                course.stars|=object.starBit;events.star=true;
            } else {
                course.coins+=object.kind==ObjectKind::RedCoin?2:1;
                if(object.kind==ObjectKind::RedCoin)++course.redCoins;
                course.health=std::min(8u,course.health+1);events.coin=true;
            }
            object.active=false;
        }
    }
    for(auto& object:course.objects)
        if(object.kind==ObjectKind::Star && object.starBit==(1U<<3) && course.redCoins==8 && !(course.stars&object.starBit))
            object.active=true;
    return events;
}

void loadCourseSave(CourseState& course,const std::filesystem::path& file) {
    int fd=::open(file.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0){check(errno==ENOENT,"cannot read SM64 progress");return;}
    unsigned char data[8];struct stat stat{};
    bool good=::fstat(fd,&stat)==0&&S_ISREG(stat.st_mode)&&stat.st_size==8&&::read(fd,data,8)==8;
    ::close(fd);
    check(good&&data[0]=='S'&&data[1]=='M'&&data[2]=='6'&&data[3]=='4'&&data[4]==1&&data[5]<=63&&data[6]==(data[5]^0xa5)&&data[7]==0,
          "invalid SM64 progress file; original preserved");
    course.stars=data[5];
    for(auto& object:course.objects)if(object.kind==ObjectKind::Star && (course.stars&object.starBit))object.active=false;
}
void saveCourse(CourseState& course,const std::filesystem::path& file) {
    const auto directory=file.parent_path();
    check(!std::filesystem::is_symlink(directory),"save folder must not be a symlink");
    std::filesystem::create_directories(directory);
    auto staging=file.string()+".pending."+std::to_string(::getpid());
    int fd=::open(staging.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    check(fd>=0,"cannot create SM64 progress transaction");
    unsigned char data[]={'S','M','6','4',1,static_cast<unsigned char>(course.stars),static_cast<unsigned char>(course.stars^0xa5),0};
    bool good=::write(fd,data,8)==8&&::fsync(fd)==0;
    if(::close(fd))good=false;
    if(!good||::rename(staging.c_str(),file.c_str())){::unlink(staging.c_str());throw std::runtime_error("SM64 progress commit failed");}
    fd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    check(fd>=0,"cannot sync SM64 progress directory");
    int result=::fsync(fd),error=errno;::close(fd);
    if(result&&error==EINVAL)::sync();else check(!result,"SM64 progress directory sync failed");
}
}
