// Read-only host probe of current global/local registration. No app integration.
#include "burst_fusion.h"
#include "local_registration.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

cv::Mat readFrame(const std::filesystem::path& directory, int index) {
    std::ifstream input(directory / (std::to_string(index) + ".rgb"), std::ios::binary);
    int width=0, height=0;
    input.read(reinterpret_cast<char*>(&width), 4);
    input.read(reinterpret_cast<char*>(&height), 4);
    if(!input || width!=4096 || height!=3072) throw std::runtime_error("Expected paired 4096x3072 diagnostic RGB frame");
    cv::Mat image(height,width,CV_8UC3);
    input.read(reinterpret_cast<char*>(image.data), image.total()*3);
    if(!input) throw std::runtime_error("Incomplete RGB diagnostic frame");
    cv::cvtColor(image,image,cv::COLOR_RGB2BGR);
    return image;
}

void writePatch(const std::filesystem::path& file,const cv::Mat& gray,const cv::Rect& roi) {
    std::ofstream out(file,std::ios::binary);
    out<<"P5\n"<<roi.width<<" "<<roi.height<<"\n255\n";
    for(int y=roi.y;y<roi.y+roi.height;++y)
        out.write(reinterpret_cast<const char*>(gray.ptr<uchar>(y)+roi.x),roi.width);
    if(!out)throw std::runtime_error("Cannot write probe patch");
}

int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"Usage: probe input-rgb-directory output-directory\n";return 2;}
    try {
        const std::filesystem::path input(argv[1]),output(argv[2]);
        std::filesystem::create_directories(output);
        cv::setNumThreads(4);
        auto reference=readFrame(input,1);
        cv::Mat refGray;cv::cvtColor(reference,refGray,cv::COLOR_BGR2GRAY);
        BurstFusionEngine engine;
        std::ofstream manifest(output/"production.json");
        manifest<<"{\"reference_index\":1,\"patches\":[";
        bool first=true;
        for(int index:{0,2,3}) {
            auto source=readFrame(input,index);cv::Mat warped,transform;
            if(!engine.alignBaseFrame(source,reference,warped,transform))
                throw std::runtime_error("Production global alignment rejected frame");
            cv::Mat srcGray;cv::cvtColor(warped,srcGray,cv::COLOR_BGR2GRAY);
            const auto local=fusion::registerLocal(refGray,srcGray);
            int patch=0;
            for(int cy:{1400,1580,1760})for(int cx:{1440,1760,2080}) {
                const cv::Rect roi(cx-64,cy-64,128,128);
                const auto prefix=std::to_string(index)+"-"+std::to_string(patch++);
                writePatch(output/(prefix+"-reference.pgm"),refGray,roi);
                writePatch(output/(prefix+"-source.pgm"),srcGray,roi);
                cv::Point2d delta;float confidence=0;
                const bool usable=local.offset(cx,cy,delta,confidence);
                const double support=double(cv::countNonZero(local.gate(roi)))/roi.area();
                if(!first)manifest<<",";first=false;
                manifest<<"{\"id\":\""<<prefix<<"\",\"x\":"<<cx<<",\"y\":"<<cy
                        <<",\"production_offset_usable\":"<<(usable?"true":"false")
                        <<",\"production_detail_at_centre\":"<<(local.hasDetail(cx,cy)?"true":"false")
                        <<",\"production_patch_support\":"<<support;
                if(usable)manifest<<",\"production_dx\":"<<delta.x<<",\"production_dy\":"<<delta.y;
                manifest<<"}";
            }
        }
        manifest<<"]}\n";
        if(!manifest)throw std::runtime_error("Cannot write probe manifest");
        std::cout<<"Exported 27 globally aligned diagnostic patch pairs; no ground-truth flow.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
}
