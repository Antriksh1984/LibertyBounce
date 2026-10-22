#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <istream>
#include <string>
#include <cstdlib>
#include <cctype>

namespace libertybounce {
// "LibertyBounceBeats 1": one beat time (ms) per line.
// "LibertyBounceBeats 2": "time strength" per line; strength 0..1 says how
// clearly beat-driven the audio is there (radio talk and adverts are ~0).
class BeatMap {
    std::vector<double> beats,strengths;
    static bool SkipSpace(const char*& text) {
        while(*text&&std::isspace(static_cast<unsigned char>(*text))) ++text;
        return *text!=0;
    }
public:
    bool Load(std::istream& stream) {
        beats.clear();strengths.clear();std::string header;
        if(!std::getline(stream,header)) return false;
        if(!header.empty()&&header.back()=='\r') header.pop_back();
        const bool weighted=header=="LibertyBounceBeats 2";
        if(header!="LibertyBounceBeats 1"&&!weighted) return false;
        std::string line;
        while(std::getline(stream,line)) {
            if(line.find_first_not_of(" \t\r")==std::string::npos) continue;
            if(line.size()>128) {beats.clear();return false;}
            char* end=nullptr;
            const double value=std::strtod(line.c_str(),&end);
            if(end==line.c_str()) {beats.clear();return false;}
            double strength=1;
            if(weighted) {
                const char* rest=end;
                if(!SkipSpace(rest)) {beats.clear();return false;}
                strength=std::strtod(rest,&end);
                if(end==rest||!std::isfinite(strength)||strength<0||strength>1) {beats.clear();return false;}
            }
            const char* rest=end;
            if(SkipSpace(rest)) {beats.clear();return false;}
            if(!std::isfinite(value)||value<0||value>43200000||beats.size()>=200000||
                (!beats.empty()&&value-beats.back()<80)) {beats.clear();return false;}
            beats.push_back(value);strengths.push_back(strength);
        }
        if(stream.bad()||beats.size()<2) {beats.clear();strengths.clear();return false;}
        return true;
    }
    bool Valid() const {return beats.size()>=2;}
    size_t Size() const {return beats.size();}
    double Phase(double position) const {
        if(!Valid()||!std::isfinite(position)) return .5;
        auto next=std::upper_bound(beats.begin(),beats.end(),position);
        size_t i=next==beats.begin()?0:next==beats.end()?beats.size()-2:size_t(next-beats.begin()-1);
        double phase=std::fmod((position-beats[i])/(beats[i+1]-beats[i])+.5,1.0);
        return phase<0?phase+1:phase;
    }
    // Beat strength at a position, interpolated between neighbouring beats.
    double Strength(double position) const {
        if(!Valid()||!std::isfinite(position)) return 1;
        auto next=std::upper_bound(beats.begin(),beats.end(),position);
        if(next==beats.begin()) return strengths.front();
        if(next==beats.end()) return strengths.back();
        const size_t i=size_t(next-beats.begin()-1);
        const double t=(position-beats[i])/(beats[i+1]-beats[i]);
        return strengths[i]+(strengths[i+1]-strengths[i])*t;
    }
    double Nearest(double position) const {
        if(!Valid()||!std::isfinite(position)) return 0;
        auto next=std::lower_bound(beats.begin(),beats.end(),position);
        if(next==beats.begin()) return *next;
        if(next==beats.end()) return beats.back();
        return position-*(next-1)<*next-position?*(next-1):*next;
    }
};
}
