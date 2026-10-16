// Included inside the shared Vulkan Context; no second context or image pool.
bool runGlobal(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& initial,
    int model,cv::TermCriteria criteria,cv::Mat& result,double& correlation,int& iterations) {
    if(!globalStages[0]){failure="global shared-memory capability unavailable";return false;}
    const int width=reference.cols,height=reference.rows,count=width*height,chunks=(count+1023)/1024;
    const int coords=80,partial=coords+2*width+2*height;
    const size_t words=(size_t(count)+3)/4,plane=words*4;
    const VkDeviceSize sizes[]{size_t(count)*16,plane*2,size_t(partial+chunks*56)*4};
    if((count+63)/64>int(properties.limits.maxComputeWorkGroupCount[0]) || !workspace(sizes))return false;
    auto* bytes=static_cast<uchar*>(buffers[1].mapped);std::memset(bytes,0,size_t(sizes[1]));
    for(int y=0;y<height;++y){std::memcpy(bytes+size_t(y)*width,reference.ptr(y),width);std::memcpy(bytes+plane+size_t(y)*width,source.ptr(y),width);}
    const std::array<int,8> shape{width,height,model,0,chunks,count,coords,partial};
    if(!dispatch(globalStages[0],sizes,shape,uint32_t((count+63)/64)))return false;
    result=initial.clone();correlation=-1;double previous=-criteria.epsilon;iterations=0;
    auto* control=static_cast<float*>(buffers[2].mapped);
    const int n=model==cv::MOTION_EUCLIDEAN ? 3 : 8,triangle=n*(n+1)/2;
    for(int iteration=0;iteration<criteria.maxCount && std::abs(correlation-previous)>=criteria.epsilon;++iteration){
        std::fill(control,control+80,0.f);
        for(int y=0;y<result.rows;++y)for(int x=0;x<3;++x)control[y*3+x]=result.at<float>(y,x);
        if(model==cv::MOTION_EUCLIDEAN){
            auto* fixed=reinterpret_cast<int32_t*>(control+coords);
            for(int x=0;x<width;++x){fixed[x]=cvRound(double(result.at<float>(0,0))*x*1024.);fixed[width+x]=cvRound(double(result.at<float>(1,0))*x*1024.);}
            for(int y=0;y<height;++y){fixed[2*width+y]=cvRound((double(result.at<float>(0,1))*y+result.at<float>(0,2))*1024.);fixed[2*width+height+y]=cvRound((double(result.at<float>(1,1))*y+result.at<float>(1,2))*1024.);}
        }
        if(!dispatch(globalStages[3],sizes,shape,uint32_t(chunks)))return false;
        ++iterations;
        for(int k=0;k<56;++k)if(!std::isfinite(control[16+k])){failure="global nonfinite statistics";return false;}
        const double vi=control[16],vt=control[17],cross=control[18];
        if(control[14]<256 || vi<=0 || vt<=0){failure="global insufficient variance";return false;}
        cv::Mat h(n,n,CV_32F),ip(n,1,CV_32F),tp(n,1,CV_32F);int at=19;
        for(int a=0;a<n;++a)for(int b=a;b<n;++b)h.at<float>(a,b)=h.at<float>(b,a)=control[at++];
        for(int a=0;a<n;++a){ip.at<float>(a)=control[19+triangle+a];tp.at<float>(a)=control[19+triangle+n+a];}
        cv::Mat inverse=h.inv(),projected=inverse*ip;
        const double denominator=cross-tp.dot(projected),numerator=vi-ip.dot(projected);
        if(denominator<=0 || !std::isfinite(denominator)||!std::isfinite(numerator)){failure="global uncorrelated observations";return false;}
        previous=correlation;correlation=cross/std::sqrt(vi*vt);
        cv::Mat error=(numerator/denominator)*tp-ip,delta=inverse*error;
        if(!std::isfinite(correlation)||!cv::checkRange(delta)){failure="global nonfinite update";return false;}
        if(model==cv::MOTION_EUCLIDEAN){
            const double theta=std::asin(double(result.at<float>(1,0)))+delta.at<float>(0);
            result.at<float>(0,0)=result.at<float>(1,1)=float(std::cos(theta));
            result.at<float>(1,0)=float(std::sin(theta));result.at<float>(0,1)=-result.at<float>(1,0);
            result.at<float>(0,2)+=delta.at<float>(1);result.at<float>(1,2)+=delta.at<float>(2);
        }else{
            const int positions[]{0,3,6,1,4,7,2,5};
            for(int a=0;a<8;++a)result.ptr<float>()[positions[a]]+=delta.at<float>(a);
        }
        if(!cv::checkRange(result) || cv::norm(result,cv::NORM_INF)>1e5){failure="global unstable transform";return false;}
    }
    return iterations>0 && std::isfinite(correlation);
}
