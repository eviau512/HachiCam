// Read-only same-domain assessment; no new motion estimator enters the app.
#include "burst_fusion.h"
#include "local_registration.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
cv::Mat raw(const fs::path& dir,int index) {
    std::ifstream f(dir/(std::to_string(index)+".rgb"),std::ios::binary);
    int w=0,h=0;f.read((char*)&w,4);f.read((char*)&h,4);
    if(!f||w!=4096||h!=3072)throw std::runtime_error("Expected paired 4096x3072 RGB");
    cv::Mat m(h,w,CV_8UC3);f.read((char*)m.data,m.total()*3);
    if(!f)throw std::runtime_error("Incomplete RGB");cv::cvtColor(m,m,cv::COLOR_RGB2BGR);return m;
}
void write(const fs::path& path,const cv::Mat& m) {
    std::ofstream f(path,std::ios::binary);f<<"P5\n"<<m.cols<<' '<<m.rows<<"\n255\n";
    for(int y=0;y<m.rows;++y)f.write((char*)m.ptr(y),m.cols);
    if(!f)throw std::runtime_error("PGM write failed");
}
cv::Mat read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);std::string magic;int w,h,peak;f>>magic>>w>>h>>peak;f.get();
    if(!f||magic!="P5"||w!=321||h!=321||peak!=255)throw std::runtime_error("Expected 321 PGM");
    cv::Mat m(h,w,CV_8U);f.read((char*)m.data,m.total());if(!f)throw std::runtime_error("Incomplete PGM");return m;
}
cv::Mat hp(const cv::Mat& image) {
    cv::Mat f,a,b;image.convertTo(f,CV_32F);cv::GaussianBlur(f,a,{0,0},.8);
    cv::GaussianBlur(f,b,{0,0},2.0);return a-b;
}
struct Assessment {bool detail=false;double score=0,ratio=0,pixelSupport=0,cc=0;};
Assessment assess(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& common,double dx,double dy) {
    Assessment out;
    if(!std::isfinite(dx)||!std::isfinite(dy)||std::hypot(dx,dy)>8)return out;
    cv::Mat rh=hp(ref),sh=hp(src),corrected,ra,sa,warped;
    cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,dx,0,1,dy);
    cv::warpAffine(sh,corrected,transform,ref.size(),cv::INTER_LINEAR|cv::WARP_INVERSE_MAP,cv::BORDER_REFLECT_101);
    cv::Mat valid=common.clone();valid.rowRange(0,10).setTo(0);valid.rowRange(valid.rows-10,valid.rows).setTo(0);
    valid.colRange(0,10).setTo(0);valid.colRange(valid.cols-10,valid.cols).setTo(0);
    valid &= (ref>8)&(ref<247);
    if(cv::countNonZero(valid)<256)return out;
    const double nr=fusion::estimateNoiseSigma(ref),ns=fusion::estimateNoiseSigma(src);
    static const double ng=[] {cv::Mat impulse=cv::Mat::zeros(25,25,CV_32F);impulse.at<float>(12,12)=1;auto b=hp(impulse);return cv::sum(b.mul(b))[0];}();
    cv::Scalar ma,mb,sta,stb;cv::meanStdDev(rh,ma,sta,valid);cv::meanStdDev(corrected,mb,stb,valid);
    const double va=std::max(0.,sta[0]*sta[0]-ng*nr*nr),vb=std::max(0.,stb[0]*stb[0]-ng*ns*ns);
    out.ratio=std::sqrt(vb/std::max(va,.01));
    out.score=(cv::mean(rh.mul(corrected),valid)[0]-ma[0]*mb[0])/std::max(std::sqrt(va*vb),.01);
    const cv::Rect interior(10,10,ref.cols-20,ref.rows-20);
    cv::Scalar unused,rd,sd;cv::meanStdDev(rh(interior),unused,rd);cv::meanStdDev(sh(interior),unused,sd);
    const double rsignal=std::max(0.,rd[0]*rd[0]-ng*nr*nr),ssignal=std::max(0.,sd[0]*sd[0]-ng*ns*ns);
    cv::GaussianBlur(ref,ra,{0,0},1.2);cv::GaussianBlur(src,sa,{0,0},1.2);
    cv::warpAffine(sa,warped,transform,ref.size(),cv::INTER_LINEAR|cv::WARP_INVERSE_MAP,cv::BORDER_REFLECT_101);
    out.cc=cv::computeECC(ra,warped,valid);
    out.detail=out.cc>=.65&&out.score>=.90&&out.ratio>=.68&&
        rsignal>=std::max(2.25,4*ng*nr*nr)&&ssignal>=std::max(2.25,4*ng*ns*ns);
    // Existing native residual bound on exactly the shared support, after mapping.
    cv::GaussianBlur(ref,ra,{0,0},.6);cv::GaussianBlur(src,sa,{0,0},.6);
    int good=0,total=cv::countNonZero(common);
    for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x)if(common.at<uchar>(y,x)) {
        double sx=x+dx,sy=y+dy;if(sx<0||sy<0||sx>src.cols-1||sy>src.rows-1)continue;
        int x0=int(sx),y0=int(sy),x1=std::min(x0+1,src.cols-1),y1=std::min(y0+1,src.rows-1);
        double fx=sx-x0,fy=sy-y0;
        double observed=(1-fy)*((1-fx)*sa.at<uchar>(y0,x0)+fx*sa.at<uchar>(y0,x1))+fy*((1-fx)*sa.at<uchar>(y1,x0)+fx*sa.at<uchar>(y1,x1));
        if(std::abs(observed-ra.at<uchar>(y,x))<=14)++good;
    }
    out.pixelSupport=total?double(good)/total:0;return out;
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: review RGB_DIR OUTPUT_DIR | assess OUTPUT_DIR");
        cv::setNumThreads(4);const fs::path output(argv[2]);fs::create_directories(output);
        if(std::string(argv[1])=="assess") {
            std::ifstream fits(output/"fits.txt");std::ofstream results(output/"post-gates.csv");
            results<<"id,method,detail,correlation,detail_ratio,pixel_support,cc\n";
            std::string id,method;double dx,dy;
            while(fits>>id>>method>>dx>>dy) {
                auto a=assess(read(output/(id+"-reference.pgm")),read(output/(id+"-source.pgm")),read(output/(id+"-mask.pgm")),dx,dy);
                results<<id<<','<<method<<','<<a.detail<<','<<a.score<<','<<a.ratio<<','<<a.pixelSupport<<','<<a.cc<<'\n';
            }
            if(!fits.eof()||!results)throw std::runtime_error("Assessment IO failed");return 0;
        }
        auto reference=raw(argv[1],1);cv::Mat rg;cv::cvtColor(reference,rg,cv::COLOR_BGR2GRAY);
        std::ofstream manifest(output/"production.json");manifest<<"{\"size\":321,\"patches\":[";bool first=true;
        BurstFusionEngine engine;
        for(int index:{0,2,3}) {
            cv::Mat warped,h;if(!engine.alignBaseFrame(raw(argv[1],index),reference,warped,h))throw std::runtime_error("Global rejection");
            cv::Mat sg;cv::cvtColor(warped,sg,cv::COLOR_BGR2GRAY);auto production=fusion::registerLocal(rg,sg);
            for(int cy:{1280,1536,1792})for(int cx:{1536,1792,2048}) {
                const cv::Rect roi(cx-160,cy-160,321,321);const auto id=std::to_string(index)+"-"+std::to_string(cx)+"-"+std::to_string(cy);
                cv::Mat r=rg(roi),s=sg(roi),common=(r>8)&(r<247)&(s>8)&(s<247);
                cv::erode(common,common,cv::getStructuringElement(cv::MORPH_RECT,{9,9}));
                auto baseline=fusion::registerLocal(r,s,false,true,common);auto p=baseline.field.at<cv::Vec3f>(0,0);
                auto reverse=fusion::registerLocal(s,r,false,true,common);auto back=reverse.field.at<cv::Vec3f>(0,0);
                write(output/(id+"-reference.pgm"),r);write(output/(id+"-source.pgm"),s);write(output/(id+"-mask.pgm"),common);
                if(!first)manifest<<',';first=false;
                manifest<<"{\"id\":\""<<id<<"\",\"original_detail\":"<<(production.detail.at<uchar>(cy/256,cx/256)?"true":"false")
                    <<",\"shared_production_usable\":"<<(p[2]>0?"true":"false")<<",\"shared_production_detail\":"<<(baseline.detail.at<uchar>(0,0)?"true":"false")
                    <<",\"shared_production_dx\":"<<p[0]<<",\"shared_production_dy\":"<<p[1]
                    <<",\"reverse_usable\":"<<(back[2]>0?"true":"false")<<",\"reverse_dx\":"<<back[0]<<",\"reverse_dy\":"<<back[1]
                    <<",\"common_fraction\":"<<double(cv::countNonZero(common))/common.total()<<"}";
            }
        }
        manifest<<"]}\n";if(!manifest)throw std::runtime_error("Manifest write failed");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
