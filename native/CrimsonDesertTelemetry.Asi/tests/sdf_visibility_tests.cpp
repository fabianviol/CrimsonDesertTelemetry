#include "sdf_visibility.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace cdt::sdf;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void PutFloat(std::array<std::uint8_t,768>& bytes,size_t offset,float value)
{std::memcpy(bytes.data()+offset,&value,sizeof(value));}
std::vector<std::uint8_t> Uniform(std::uint16_t half)
{
    std::vector<std::uint8_t> bytes(size_t{128}*64*1040*2);
    for(size_t i=0;i<bytes.size();i+=2){bytes[i]=static_cast<std::uint8_t>(half);bytes[i+1]=static_cast<std::uint8_t>(half>>8);}
    return bytes;
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==10&&std::string_view(argv[1])=="--live")
        {
            std::ifstream volumeFile(argv[2],std::ios::binary),constantsFile(argv[3],std::ios::binary);
            std::vector<std::uint8_t> volume((std::istreambuf_iterator<char>(volumeFile)),{});
            std::array<std::uint8_t,768> constants{};constantsFile.read(reinterpret_cast<char*>(constants.data()),constants.size());
            Require(volumeFile.good()||volumeFile.eof(),"Live payload read failed");
            Require(constantsFile.gcount()==static_cast<std::streamsize>(constants.size()),"Live constants read failed");
            const std::array<float,3> camera{std::stof(argv[4]),std::stof(argv[5]),std::stof(argv[6])};
            const std::array<float,3> target{std::stof(argv[7]),std::stof(argv[8]),std::stof(argv[9])};
            Publish(std::move(volume),constants,camera,1,100,true);
            const auto trace=Trace(target,100);
            std::cout<<VerdictName(trace.verdict)<<" closest="<<trace.closest<<" at="<<trace.closestAt<<'\n';
            return trace.available?0:2;
        }
        Require(argc==1,"Usage: sdf test [--live payload constants camX camY camZ targetX targetY targetZ]");
        std::array<std::uint8_t,768> constants{};
        PutFloat(constants,0x10,1.f/32);PutFloat(constants,0x14,1.f/16);PutFloat(constants,0x18,1.f/32);
        for(unsigned level=0;level<8;++level)PutFloat(constants,0x140+16*level+12,1.f);
        Publish(Uniform(0x3c00),constants,{0,0,0},42,100,true);
        auto trace=Trace({5,0,0},100);
        Require(trace.available&&trace.verdict==Verdict::Clear&&trace.closest==1,
            "Uniform positive signed distance must trace clear");
        Publish(Uniform(0xbc00),constants,{0,0,0},43,200,true);
        trace=Trace({5,0,0},200);
        Require(trace.available&&trace.verdict==Verdict::Blocked&&trace.value==-1&&trace.at==.6,
            "Uniform negative signed distance must block at the first fixed sample");
        Require(!CurrentStatus(8201).available&&CurrentStatus(8201).reason=="stale-sdf-volume",
            "Old SDF volume must fail closed");
        Publish(Uniform(0x3c00),constants,{0,0,0},44,300,false);
        Require(!Trace({5,0,0},300).available&&CurrentStatus(300).reason=="unbracketed-sdf-context",
            "Unbracketed CPU context must not produce a LOS verdict");
        // The production path uses this light capture's camera, not the older
        // camera stored with the SDF. A wall separates x=0 from x=8, but not x=4.
        auto wall=Uniform(0x3c00);
        for(unsigned z=0;z<1040;++z)for(unsigned y=0;y<64;++y)for(unsigned x=8;x<12;++x)
        {
            const size_t at=((size_t{z}*64+y)*128+x)*2;
            wall[at]=0;wall[at+1]=0xbc;
        }
        Publish(std::move(wall),constants,{0,0,0},45,400,true);
        const std::array<std::array<float,3>,2> targets{{{8,0,0},{-8,0,0}}};
        auto batch=TraceBatch({0,0,0},targets,450);
        Require(batch.status.available&&batch.traces.size()==2&&
            batch.traces[0].verdict==Verdict::Blocked&&batch.traces[1].verdict==Verdict::Clear,
            "Production batch must preserve a geometric wall and a clear opposite-direction target");
        const auto retainedSequence=batch.status.sequence;
        Require(batch.traces[0].sequence==retainedSequence&&batch.traces[1].sequence==retainedSequence&&
            batch.status.capturedTickMilliseconds==400,"One batch must use one volume identity");
        batch=TraceBatch({4,0,0},std::span(targets).first(1),450);
        Require(batch.traces[0].verdict==Verdict::Clear&&Trace(targets[0],450).verdict==Verdict::Blocked,
            "Production trace must use the explicit fresh reference, not the stored SDF camera");
        batch=TraceBatch({0,0,0},targets,1901);
        Require(!batch.status.available&&batch.status.reason=="stale-sdf-volume"&&
            batch.traces[0].verdict==Verdict::Unavailable,"Production must reject volumes older than 1500 ms");
        std::vector<std::array<float,3>> excess(MaximumBatchTargets+1);
        Require(!TraceBatch({0,0,0},excess,450).status.available,"Production trace budget must be bounded");
        Clear();Require(!CurrentStatus(300).available,"Cleared SDF bridge retained data");
        std::cout<<"PASS fixed Variant A clear/blocked, freshness and context fail-closed behavior\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
