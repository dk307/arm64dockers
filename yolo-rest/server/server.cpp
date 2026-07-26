#include <ncnn/net.h>
#include <ncnn/cpu.h>
#if NCNN_VULKAN
#include <ncnn/gpu.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "httplib.h"
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include <map>
#include <memory>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* COCO[80]={"person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"};
static int coco_id(const std::string&s){for(int i=0;i<80;i++)if(s==COCO[i])return i;return -1;}
static std::string json_escape(const std::string&s){
    std::string r; r.reserve(s.size()+8);
    for(char c:s){switch(c){
        case '"': r+="\\\""; break; case '\\': r+="\\\\"; break;
        case '\n': r+="\\n"; break; case '\r': r+="\\r"; break; case '\t': r+="\\t"; break;
        default: r+=c;}}
    return r;
}

struct Object { float x,y,w,h; int label; float prob; };
static inline float iou(const Object&a,const Object&b){
    float x1=std::max(a.x,b.x),y1=std::max(a.y,b.y),x2=std::min(a.x+a.w,b.x+b.w),y2=std::min(a.y+a.h,b.y+b.h);
    float iw=std::max(0.f,x2-x1),ih=std::max(0.f,y2-y1),i=iw*ih; return i/(a.w*a.h+b.w*b.h-i+1e-6f);
}
static void nms(std::vector<Object>&o,float t){
    std::sort(o.begin(),o.end(),[](const Object&a,const Object&b){return a.prob>b.prob;});
    std::vector<Object> r; std::vector<char> sup(o.size(),0);
    for(size_t i=0;i<o.size();++i){ if(sup[i])continue; r.push_back(o[i]);
        for(size_t j=i+1;j<o.size();++j) if(!sup[j]&&o[i].label==o[j].label&&iou(o[i],o[j])>t) sup[j]=1; }
    o.swap(r);
}

static const std::map<std::string,std::string> MODEL_TYPES={
    {"yolo26n","/models/yolo26n_opt.param"},
    {"yolo26m","/models/yolo26m_opt.param"},
};
static std::string g_default_model_type="yolo26m";
static std::string g_default_model_path="";

static std::string resolve_model(const std::string& model_type,const std::string& model_path){
    if(!model_path.empty()) return model_path;
    if(!model_type.empty()){
        auto it=MODEL_TYPES.find(model_type);
        if(it!=MODEL_TYPES.end()) return it->second;
        return "";
    }
    if(!g_default_model_path.empty()) return g_default_model_path;
    auto it=MODEL_TYPES.find(g_default_model_type);
    if(it!=MODEL_TYPES.end()) return it->second;
    return "";
}

static int g_threads=0;
struct Detector {
    ncnn::Net net; bool gpu=false; int target=640;
    bool load(const std::string&param,const std::string&bin,bool use_gpu){
        gpu=use_gpu; net.opt.use_vulkan_compute=use_gpu;
        if(g_threads>0) net.opt.num_threads=g_threads; else if(use_gpu) net.opt.num_threads=1;
        net.opt.use_fp16_packed=!use_gpu; net.opt.use_fp16_storage=!use_gpu; net.opt.use_fp16_arithmetic=!use_gpu;
        net.opt.use_int8_inference=true;
        if(net.load_param(param.c_str()))return false;
        if(net.load_model(bin.c_str()))return false;
        return true;
    }
    std::vector<Object> detect(const unsigned char* rgb,int iw,int ih,double& infer_ms,
                               float conf,float nmsT,const std::set<int>* allowed){
        int w=iw,h=ih; float scale;
        if(w>h){scale=(float)target/w; w=target; h=int(ih*scale);} else {scale=(float)target/h; h=target; w=int(iw*scale);}
        ncnn::Mat in=ncnn::Mat::from_pixels_resize(rgb,ncnn::Mat::PIXEL_RGB,iw,ih,w,h);
        int wpad=target-w,hpad=target-h; ncnn::Mat in_pad;
        ncnn::copy_make_border(in,in_pad,0,hpad,0,wpad,ncnn::BORDER_CONSTANT,114.f);
        const float norm[3]={1/255.f,1/255.f,1/255.f}; in_pad.substract_mean_normalize(0,norm);
        auto t0=std::chrono::high_resolution_clock::now();
        ncnn::Extractor ex=net.create_extractor(); ex.input("in0",in_pad);
        ncnn::Mat out; ex.extract("out0",out);
        infer_ms=std::chrono::duration<double,std::milli>(std::chrono::high_resolution_clock::now()-t0).count();
        int na,nf; bool rf; if(out.w>out.h){na=out.w;nf=out.h;rf=true;}else{na=out.h;nf=out.w;rf=false;}
        int nc=nf-4; std::vector<Object> objs;
        for(int a=0;a<na;++a){
            auto val=[&](int f){return rf?out.row(f)[a]:out.row(a)[f];};
            float bx=val(0),by=val(1),bw=val(2),bh=val(3); int best=-1; float bestp=conf;
            for(int cc=0;cc<nc;++cc){ if(allowed && !allowed->count(cc)) continue;
                float s=val(4+cc); if(s>bestp){bestp=s;best=cc;} }
            if(best<0)continue;
            Object o; o.x=(bx-bw*0.5f)/scale; o.y=(by-bh*0.5f)/scale; o.w=bw/scale; o.h=bh/scale; o.label=best; o.prob=bestp;
            objs.push_back(o);
        }
        nms(objs,nmsT);
        for(auto&o:objs){o.x=std::max(0.f,o.x);o.y=std::max(0.f,o.y);o.w=std::min((float)iw-o.x,o.w);o.h=std::min((float)ih-o.y,o.h);}
        return objs;
    }
};
static std::map<std::string,std::shared_ptr<Detector>> g_cache; static std::mutex g_mu;
std::shared_ptr<Detector> get_det(const std::string& param_path,bool gpu){
    std::string key=param_path+(gpu?"|g":"|c");
    std::lock_guard<std::mutex> lk(g_mu);
    auto it=g_cache.find(key); if(it!=g_cache.end())return it->second;
    std::string bin=param_path; auto pos=bin.rfind(".param"); if(pos==std::string::npos) return nullptr;
    bin.replace(pos,6,".bin");
    auto d=std::make_shared<Detector>();
    if(!d->load(param_path,bin,gpu)) return nullptr;
    g_cache[key]=d; return d;
}
static std::set<int> parse_classes(const std::string& s){
    std::set<int> out; size_t i=0,n=s.size();
    while(i<=n){ size_t j=s.find(',',i); std::string tok=s.substr(i, j==std::string::npos?n-i:j-i);
        while(!tok.empty()&&isspace((unsigned char)tok.front())) tok.erase(tok.begin());
        while(!tok.empty()&&isspace((unsigned char)tok.back())) tok.pop_back();
        if(!tok.empty()){ bool num=true; for(char ch:tok) if(!isdigit((unsigned char)ch)){num=false;break;}
            if(num){int id=atoi(tok.c_str()); if(id>=0&&id<80)out.insert(id);} else {int id=coco_id(tok); if(id>=0)out.insert(id);} }
        if(j==std::string::npos)break; i=j+1; }
    return out;
}
static std::string dets_json(double infer_ms,const std::vector<Object>&objs,const std::string* name){
    std::string j="{"; if(name){j+="\"image\":\""; j+=json_escape(*name); j+="\",";}
    j+="\"infer_ms\":"+std::to_string(infer_ms)+",\"detections\":[";
    for(size_t i=0;i<objs.size();++i){const auto&o=objs[i];char buf[256];
        snprintf(buf,sizeof(buf),"%s{\"label\":\"%s\",\"class\":%d,\"score\":%.4f,\"box\":[%.1f,%.1f,%.1f,%.1f]}",i?",":"",(o.label>=0&&o.label<80)?COCO[o.label]:"?",o.label,o.prob,o.x,o.y,o.w,o.h);
        j+=buf;}
    j+="]}"; return j;
}
static std::string models_json(){
    std::string j="{\"models\":[";
    bool first=true;
    for(const auto& kv:MODEL_TYPES){
        if(!first) j+=",";
        j+="{\"name\":\""+kv.first+"\",\"param\":\""+kv.second+"\"}";
        first=false;
    }
    j+="]}"; return j;
}
int main(int argc,char**argv){
    int port=18080; int threads=0;
    for(int i=1;i<argc;i++){ std::string a=argv[i];
        if(a=="--port"&&i+1<argc)port=atoi(argv[++i]);
        else if(a=="--model_type"&&i+1<argc) g_default_model_type=argv[++i];
        else if(a=="--model"&&i+1<argc) g_default_model_path=argv[++i];
        else if(a=="--threads"&&i+1<argc)threads=atoi(argv[++i]);
        else if(a=="-h"||a=="--help"){printf("usage: yolo_server [--port N] [--model_type yolo26n|yolo26m] [--model /full/path.ncnn.param] [--threads N]\n");return 0;}
        else if(!a.empty()&&a[0]!='-')port=atoi(a.c_str());
    }
    g_threads=threads;
    int gpus=0;
#if NCNN_VULKAN
    if(ncnn::create_gpu_instance()){fprintf(stderr,"warn: no vulkan gpu\n");}
    gpus=ncnn::get_gpu_count();
#endif
    fprintf(stderr,"yolo-rest gpus=%d threads=%d default_model_type=%s\n",gpus,g_threads,g_default_model_type.c_str());
    httplib::Server svr;
    svr.set_payload_max_length(256ull*1024*1024);
    svr.Get("/health",[&](const httplib::Request&,httplib::Response&res){
        std::string j="{\"status\":\"ok\",\"gpus\":"+std::to_string(gpus)
            +",\"threads\":"+std::to_string(g_threads)
            +",\"default_model_type\":\""+g_default_model_type+"\","
            +"\"default_model\":\""+resolve_model("","")+"\","
            +"\"models\":[";
        bool first=true;
        for(const auto& kv:MODEL_TYPES){
            if(!first) j+=",";
            j+="\""+kv.first+"\"";
            first=false;
        }
        j+="]}";
        res.set_content(j,"application/json");
    });
    svr.Get("/models",[&](const httplib::Request&,httplib::Response&res){
        res.set_content(models_json(),"application/json");
    });
    svr.Post("/detect",[&](const httplib::Request&req,httplib::Response&res){
        std::string model_type = req.has_param("model_type")?req.get_param_value("model_type"):"";
        std::string model_path = req.has_param("model")?req.get_param_value("model"):"";
        std::string model = resolve_model(model_type,model_path);
        if(model.empty()){res.status=400;res.set_content("{\"error\":\"unknown model_type: use 'yolo26n' or 'yolo26m', or provide ?model=/full/path.ncnn.param\"}","application/json");return;}
        bool gpu=false;
#if NCNN_VULKAN
        gpu = req.has_param("device")&&req.get_param_value("device")=="gpu"; if(gpu&&gpus<=0)gpu=false;
#endif
        float conf = req.has_param("conf")? (float)atof(req.get_param_value("conf").c_str()) : 0.25f;
        std::set<int> allowed; const std::set<int>* af=nullptr;
        if(req.has_param("classes")){ allowed=parse_classes(req.get_param_value("classes")); if(!allowed.empty()) af=&allowed; }
        auto det=get_det(model,gpu);
        if(!det){res.status=400;res.set_content("{\"error\":\"model load failed: check model file exists at "+json_escape(model)+"\"}","application/json");return;}
        std::string devs = gpu?"gpu":"cpu";
        if(req.is_multipart_form_data()){
            std::string arr="["; int nimg=0; double total=0;
            for(const auto& kv : req.files){ const auto& fd=kv.second; const std::string& content=fd.content;
                if(nimg) arr+=",";
                int w,h,ch; unsigned char* rgb=stbi_load_from_memory((const unsigned char*)content.data(),(int)content.size(),&w,&h,&ch,3);
                std::string nm=fd.filename.empty()?kv.first:fd.filename;
                if(!rgb){ arr+="{\"image\":\""+json_escape(nm)+"\",\"error\":\"bad image\"}"; nimg++; continue; }
                double ims=0; auto objs=det->detect(rgb,w,h,ims,conf,0.45f,af); stbi_image_free(rgb);
                arr+=dets_json(ims,objs,&nm); total+=ims; nimg++;
            }
            arr+="]";
            res.set_content("{\"model\":\""+model+"\",\"device\":\""+devs+"\",\"count\":"+std::to_string(nimg)+",\"total_infer_ms\":"+std::to_string(total)+",\"results\":"+arr+"}","application/json");
            return;
        }
        int w,h,ch; unsigned char* rgb=stbi_load_from_memory((const unsigned char*)req.body.data(),(int)req.body.size(),&w,&h,&ch,3);
        if(!rgb){res.status=400;res.set_content("{\"error\":\"bad image\"}","application/json");return;}
        double ims=0; auto objs=det->detect(rgb,w,h,ims,conf,0.45f,af); stbi_image_free(rgb);
        res.set_content("{\"model\":\""+model+"\",\"device\":\""+devs+"\","+dets_json(ims,objs,nullptr).substr(1),"application/json");
    });
    svr.new_task_queue = []{ return new httplib::ThreadPool(1); };
    fprintf(stderr,"yolo-rest listening on 0.0.0.0:%d\n",port);
    svr.listen("0.0.0.0",port);
#if NCNN_VULKAN
    ncnn::destroy_gpu_instance();
#endif
    return 0;
}
