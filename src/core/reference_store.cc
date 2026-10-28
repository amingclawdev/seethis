#include "core/reference.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

namespace seethis::core {
namespace {
constexpr std::size_t kMaxBytes = 256 * 1024 * 1024;
bool Finite(double x) { return std::isfinite(x); }
bool ValidRect(Rect r) {
  return Finite(r.x) && Finite(r.y) && Finite(r.width) && Finite(r.height) &&
         r.width > 0 && r.height > 0 && Finite(r.x+r.width) && Finite(r.y+r.height);
}
bool Same(Rect a, Rect b) {
  return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
bool Same(Transform a, Transform b) {
  return a.a==b.a && a.d==b.d && a.tx==b.tx && a.ty==b.ty;
}
bool ValidStamp(Stamp t) { return t.utc_us>0 && t.monotonic_us>0; }
bool SafeID(const std::string& s) {
  return s.size()==32 && std::all_of(s.begin(),s.end(),[](char c) {
    return (c>='0' && c<='9') || (c>='a' && c<='f'); });
}
std::string Hex(const Bytes& bytes) {
  static constexpr char digits[]="0123456789abcdef";
  std::string out; out.reserve(bytes.size()*2);
  for (auto b:bytes) { out+=digits[b>>4]; out+=digits[b&15]; }
  return out;
}
class Sha256State {
 public:
  void Update(const std::uint8_t* data,std::size_t size) {
    total_+=size;
    while(size>0) {
      const auto count=std::min(size,block_.size()-used_);
      std::memcpy(block_.data()+used_,data,count);
      used_+=count;data+=count;size-=count;
      if(used_==block_.size()) {Transform(block_.data());used_=0;}
    }
  }
  void Update(std::string_view value) {
    Update(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());
  }
  std::string Finish() {
    const auto bits=static_cast<std::uint64_t>(total_)*8;
    std::array<std::uint8_t,128> padding{};padding[0]=0x80;
    const auto pad=used_<56?56-used_:120-used_;
    Update(padding.data(),pad);
    std::array<std::uint8_t,8> length{};
    for(int index=0;index<8;++index)
      length[static_cast<std::size_t>(7-index)]=
          static_cast<std::uint8_t>(bits>>(index*8));
    Update(length.data(),length.size());
    Bytes digest;digest.reserve(32);
    for(const auto value:h_)for(int index=3;index>=0;--index)
      digest.push_back(static_cast<std::uint8_t>(value>>(index*8)));
    return Hex(digest);
  }
 private:
  void Transform(const std::uint8_t* input) {
    static constexpr std::uint32_t k[64]={
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t w[64];
    for(int index=0;index<16;++index) {
      const auto p=input+index*4;
      w[index]=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
          (std::uint32_t(p[2])<<8)|p[3];
    }
    for(int index=16;index<64;++index) {
      const auto s0=std::rotr(w[index-15],7)^std::rotr(w[index-15],18)^
          (w[index-15]>>3);
      const auto s1=std::rotr(w[index-2],17)^std::rotr(w[index-2],19)^
          (w[index-2]>>10);
      w[index]=w[index-16]+s0+w[index-7]+s1;
    }
    auto a=h_[0],bb=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],z=h_[7];
    for(int index=0;index<64;++index) {
      const auto t1=z+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+
          ((e&f)^((~e)&g))+k[index]+w[index];
      const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+
          ((a&bb)^(a&c)^(bb&c));
      z=g;g=f;f=e;e=d+t1;d=c;c=bb;bb=a;a=t1+t2;
    }
    h_[0]+=a;h_[1]+=bb;h_[2]+=c;h_[3]+=d;
    h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=z;
  }
  std::array<std::uint32_t,8> h_={0x6a09e667,0xbb67ae85,0x3c6ef372,
      0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<std::uint8_t,64> block_{};
  std::size_t used_=0,total_=0;
};
void BE32(Bytes& out, std::uint32_t x) {
  for (int i=3;i>=0;--i) out.push_back(static_cast<std::uint8_t>(x>>(i*8)));
}
std::uint32_t CRC(const Bytes& b) {
  return static_cast<std::uint32_t>(crc32(0,b.data(),static_cast<uInt>(b.size())));
}
void Chunk(Bytes& out,const char* type,const Bytes& data) {
  BE32(out,static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
  auto crc=crc32(0,reinterpret_cast<const Bytef*>(type),4);
  if(!data.empty())crc=crc32(crc,data.data(),static_cast<uInt>(data.size()));
  BE32(out,static_cast<std::uint32_t>(crc));
}
// Decode only the canonical PNG subset emitted here. Reject malformed images,
// changed dimensions and invalid pixel streams even if an attacker recomputes hashes.
std::optional<Pixels> DecodeLegacyPng(const Image& image) {
  const Bytes& b=image.bytes;
  const Bytes signature={137,80,78,71,13,10,26,10};
  if(b.size()<8 || !std::equal(signature.begin(),signature.end(),b.begin()))return {};
  auto be=[&](std::size_t p)->std::uint32_t {
    return (std::uint32_t(b[p])<<24)|(std::uint32_t(b[p+1])<<16)|(std::uint32_t(b[p+2])<<8)|b[p+3];
  };
  Bytes compressed;std::size_t p=8;int stage=0;
  while(p+12<=b.size()) {
    const auto n=be(p);if(n>b.size()-p-12)return {};
    const std::string type(b.begin()+p+4,b.begin()+p+8);
    Bytes crc(b.begin()+p+4,b.begin()+p+8+n);if(CRC(crc)!=be(p+8+n))return {};
    if(stage==0) {
      if(type!="IHDR" || n!=13 || be(p+8)!=image.width || be(p+12)!=image.height ||
         b[p+16]!=8 || b[p+17]!=6 || b[p+18]!=0 || b[p+19]!=0 || b[p+20]!=0)return {};
    } else if(stage==1) {if(type!="sRGB" || n!=1 || b[p+8]!=0)return {};}
    else if(stage==2) {if(type!="IDAT")return {};compressed.assign(b.begin()+p+8,b.begin()+p+8+n);}
    else if(stage==3) {if(type!="IEND" || n!=0)return {};}
    else return {};
    ++stage;p+=n+12;
  }
  if(stage!=4 || p!=b.size() || compressed.size()<6 || compressed[0]!=0x78 || compressed[1]!=1)return {};
  Bytes raw;std::size_t pos=2;bool last=false;
  const std::size_t expected=(std::size_t(image.width)*4+1)*image.height;
  while(!last) {
    if(pos+5>compressed.size()-4 || compressed[pos]>1)return {};
    last=compressed[pos++]==1;
    const unsigned n=compressed[pos]|(unsigned(compressed[pos+1])<<8);
    const unsigned inverse=compressed[pos+2]|(unsigned(compressed[pos+3])<<8);pos+=4;
    if(raw.size()>expected)return {};
    const auto remaining=expected-raw.size();
    const auto required=std::min<std::size_t>(65535,remaining);
    if((n^inverse)!=65535 || n!=required ||
       last!=(n==remaining) || n>compressed.size()-4-pos)return {};
    raw.insert(raw.end(),compressed.begin()+pos,compressed.begin()+pos+n);pos+=n;
  }
  if(pos+4!=compressed.size() || raw.size()!=expected)return {};
  std::uint32_t adler_a=1,adler_b=0;
  for(const auto byte:raw) {
    adler_a=(adler_a+byte)%65521;
    adler_b=(adler_b+adler_a)%65521;
  }
  const auto expected_adler=(std::uint32_t(compressed[pos])<<24)|
      (std::uint32_t(compressed[pos+1])<<16)|
      (std::uint32_t(compressed[pos+2])<<8)|compressed[pos+3];
  if(((adler_b<<16)|adler_a)!=expected_adler)return {};
  Pixels pixels{image.width,image.height,{}};pixels.rgba.reserve(std::size_t(image.width)*image.height*4);
  for(std::size_t y=0;y<image.height;++y) {
    const auto start=y*(std::size_t(image.width)*4+1);if(raw[start]!=0)return {};
    pixels.rgba.insert(pixels.rgba.end(),raw.begin()+start+1,raw.begin()+start+1+image.width*4);
  }
  return pixels;
}

struct PngInspection {
  Pixels pixels;
  std::string rgba_sha256;
};
std::optional<PngInspection> InspectPng(const Image& image,Rect bounds,
                                        bool collect) {
  if(!ValidRect(bounds)||bounds.x<0||bounds.y<0||
     bounds.x+bounds.width>image.width||bounds.y+bounds.height>image.height)
    return {};
  const auto x=static_cast<std::size_t>(bounds.x);
  const auto y=static_cast<std::size_t>(bounds.y);
  const auto width=static_cast<std::size_t>(bounds.width);
  const auto height=static_cast<std::size_t>(bounds.height);
  if(bounds.x!=x||bounds.y!=y||bounds.width!=width||bounds.height!=height)
    return {};
  const Bytes& bytes=image.bytes;
  const std::array<std::uint8_t,8> signature={137,80,78,71,13,10,26,10};
  if(bytes.size()<8||!std::equal(signature.begin(),signature.end(),bytes.begin()))
    return {};
  auto be=[&](std::size_t offset) {
    return (std::uint32_t(bytes[offset])<<24)|
        (std::uint32_t(bytes[offset+1])<<16)|
        (std::uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
  };
  z_stream stream{};bool initialized=false,finished=false,saw_idat=false;
  std::size_t offset=8,row_index=0,row_used=0;
  const auto row_size=std::size_t(image.width)*4+1;
  Bytes row(row_size);
  PngInspection result;result.pixels.width=static_cast<std::uint32_t>(width);
  result.pixels.height=static_cast<std::uint32_t>(height);
  if(collect)result.pixels.rgba.reserve(width*height*4);
  Sha256State content_hash;
  content_hash.Update(std::string_view("seethis-rgba8-v1\0",17));
  std::array<std::uint8_t,8> dimensions{};
  for(int index=0;index<4;++index) {
    dimensions[static_cast<std::size_t>(3-index)]=
        static_cast<std::uint8_t>(width>>(index*8));
    dimensions[static_cast<std::size_t>(7-index)]=
        static_cast<std::uint8_t>(height>>(index*8));
  }
  content_hash.Update(dimensions.data(),dimensions.size());
  int stage=0;bool valid=true;
  const auto consume_row=[&]() {
    if(row_index>=image.height||row[0]!=0)return false;
    if(row_index>=y&&row_index<y+height) {
      const auto* begin=row.data()+1+x*4;
      content_hash.Update(begin,width*4);
      if(collect)result.pixels.rgba.insert(result.pixels.rgba.end(),begin,
                                           begin+width*4);
    }
    ++row_index;row_used=0;return true;
  };
  while(valid&&offset+12<=bytes.size()) {
    const auto size=be(offset);
    if(size>bytes.size()-offset-12){valid=false;break;}
    const auto* type=bytes.data()+offset+4;
    const auto* data=bytes.data()+offset+8;
    auto crc=crc32(0,type,4);crc=crc32(crc,data,size);
    if(static_cast<std::uint32_t>(crc)!=be(offset+8+size)){valid=false;break;}
    const std::string_view name(reinterpret_cast<const char*>(type),4);
    if(stage==0) {
      if(name!="IHDR"||size!=13||be(offset+8)!=image.width||
         be(offset+12)!=image.height||data[8]!=8||data[9]!=6||data[10]!=0||
         data[11]!=0||data[12]!=0)valid=false;
      else stage=1;
    } else if(stage==1) {
      if(name!="sRGB"||size!=1||data[0]!=0)valid=false;
      else stage=2;
    } else if(name=="IDAT"&&stage==2) {
      if(finished){valid=false;break;}
      saw_idat=true;
      if(!initialized) {
        if(inflateInit(&stream)!=Z_OK){valid=false;break;}
        initialized=true;
      }
      stream.next_in=const_cast<Bytef*>(data);stream.avail_in=size;
      while(valid&&stream.avail_in>0) {
        stream.next_out=row.data()+row_used;
        stream.avail_out=static_cast<uInt>(row.size()-row_used);
        const auto before_in=stream.avail_in,before_out=stream.avail_out;
        const int status=inflate(&stream,Z_NO_FLUSH);
        row_used+=before_out-stream.avail_out;
        if(row_used==row.size()&&!consume_row())valid=false;
        if(status==Z_STREAM_END) {
          finished=true;
          if(stream.avail_in!=0)valid=false;
          break;
        }
        if(status!=Z_OK||(before_in==stream.avail_in&&
                          before_out==stream.avail_out))valid=false;
      }
    } else if(name=="IEND"&&stage==2) {
      if(size!=0||!saw_idat||!finished||row_used!=0||
         row_index!=image.height)valid=false;
      stage=3;
    } else valid=false;
    offset+=static_cast<std::size_t>(size)+12;
    if(stage==3)break;
  }
  if(initialized)inflateEnd(&stream);
  if(!valid||stage!=3||offset!=bytes.size())return {};
  result.rgba_sha256=content_hash.Finish();
  return result;
}
template<class A> void Fields(A& a, Rect& v) { a(v.x,v.y,v.width,v.height); }
template<class A> void Fields(A& a, Transform& v) { a(v.a,v.d,v.tx,v.ty); }
template<class A> void Fields(A& a, Stamp& v) { a(v.utc_us,v.monotonic_us); }
template<class A> void Fields(A& a, Point2D& v) { a(v.x,v.y); }
template<class A> void Fields(A& a, DisplayPoint& v) { a(v.display_id,v.unit,v.position,v.backing_scale); }
template<class A> void Fields(A& a, FrozenDisplay& v) {
  a(v.uuid,v.id,v.logical,v.pixels,v.scale,v.logical_to_pixels);
}
template<class A> void Fields(A& a, Context& v) {
  a(v.pid,v.window_pid,v.self_pid,v.window_id,v.app_name,v.bundle_id,v.executable,
    v.window_title,v.selection_method,v.window_layer,v.window,v.quartz_to_appkit_top,v.observed,
    v.displays,v.excluded_window_ids,v.exclusion_method);
}
template<class A> void Fields(A& a, PageIdentity& v) {
  a(v.provider,v.provider_version,v.bundle_id,v.browser_pid,v.window_id,
    v.window_bounds,v.process_start_identity_us,v.opaque_tab_id,
    v.navigation_digest);
}
template<class A> void Fields(A& a, Image& v) {
  a(v.width,v.height,v.format,v.color_space,v.sha256,v.bytes);
}
template<class A> void Fields(A& a, ReferenceRegion& v) {
  a(v.path,v.region,v.crop_pixels);
}
template<class A> void Fields(A& a, Reference& v) {
  a(v.schema,v.id,v.mark_id,v.context,v.requested,v.image_completed,v.circle_completed,
    v.clock,v.capture_api,v.provenance,v.path,v.region,v.crop_pixels,v.global_to_source,
    v.crop_margin,v.source,v.crop);
  if(v.schema>=2)a(v.regions);
}
struct Writer {
  Bytes bytes;
  template<class... T> void operator()(T&... v) { (One(v),...); }
  template<class T> void One(T& v) {
    if constexpr(std::is_arithmetic_v<T> || std::is_enum_v<T>) {
      std::uint64_t n;
      if constexpr(std::is_same_v<T,double>) n=std::bit_cast<std::uint64_t>(v);
      else n=static_cast<std::uint64_t>(v);
      for(int i=7;i>=0;--i) bytes.push_back(static_cast<std::uint8_t>(n>>(i*8)));
    } else Fields(*this,v);
  }
  void One(std::string& v) { auto n=v.size(); One(n); bytes.insert(bytes.end(),v.begin(),v.end()); }
  void One(Bytes& v) { auto n=v.size(); One(n); bytes.insert(bytes.end(),v.begin(),v.end()); }
  template<class T> void One(std::vector<T>& v) { auto n=v.size(); One(n); for(auto& x:v) One(x); }
};
struct Reader {
  const Bytes& bytes; std::size_t offset=0;
  template<class... T> void operator()(T&... v) { (One(v),...); }
  void Require(std::size_t n) { if(n>bytes.size()-offset) throw std::runtime_error("truncated record"); }
  template<class T> void One(T& v) {
    if constexpr(std::is_arithmetic_v<T> || std::is_enum_v<T>) {
      Require(8); std::uint64_t n=0; for(int i=0;i<8;++i) n=(n<<8)|bytes[offset++];
      if constexpr(std::is_same_v<T,double>) v=std::bit_cast<double>(n);
      else v=static_cast<T>(n);
    } else Fields(*this,v);
  }
  void One(std::string& v) { std::uint64_t n; One(n); Require(n); v.assign(bytes.begin()+offset,bytes.begin()+offset+n); offset+=n; }
  void One(Bytes& v) { std::uint64_t n; One(n); Require(n); v.assign(bytes.begin()+offset,bytes.begin()+offset+n); offset+=n; }
  template<class T> void One(std::vector<T>& v) {
    std::uint64_t n; One(n); if(n>100000 || n>(bytes.size()-offset)/8) throw std::runtime_error("invalid count");
    v.resize(n); for(auto& x:v) One(x);
  }
};
Bytes Pack(const StoredReference& ref) {
  Writer w; auto record=ref.record, text=ref.clipboard_text, png=ref.clipboard_png;
  w(record,text,png); auto digest=Sha256(w.bytes);
  Bytes out(digest.begin(),digest.end()); out.insert(out.end(),w.bytes.begin(),w.bytes.end()); return out;
}
std::optional<StoredReference> Unpack(const Bytes& data) {
  if(data.size()<64 || data.size()>kMaxBytes) return {};
  Bytes body(data.begin()+64,data.end());
  if(std::string(data.begin(),data.begin()+64)!=Sha256(body)) return {};
  try {
    StoredReference ref; Reader r{body}; r(ref.record,ref.clipboard_text,ref.clipboard_png);
    if(r.offset!=body.size()) return {};
    auto value=Deserialize(ref.record); if(!value || !Validate(*value)) return {};
    ref.value=std::move(*value); const auto canonical=Canonical(ref.value);
    if(canonical.record!=ref.record || canonical.clipboard_text!=ref.clipboard_text ||
       canonical.clipboard_png!=ref.clipboard_png) return {};
    return ref;
  } catch(const std::exception&) { return {}; }
}
Bytes ReadFile(const std::filesystem::path& path) {
  const auto size=std::filesystem::file_size(path);
  if(size>kMaxBytes) throw std::runtime_error("record exceeds size limit");
  std::ifstream in(path,std::ios::binary); Bytes data(size);
  if(!in.read(reinterpret_cast<char*>(data.data()),data.size())) throw std::runtime_error("read failed");
  return data;
}
std::string Sha256File(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("read failed");
  Sha256State state;std::array<std::uint8_t,64*1024> buffer{};
  while(input) {
    input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
    const auto count=input.gcount();if(count>0)state.Update(buffer.data(),count);
  }
  if(!input.eof())throw std::runtime_error("read failed");
  return state.Finish();
}
}  // namespace

Stamp Now() {
  return {std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
}
std::string Sha256(const Bytes& input) {
  Sha256State state;state.Update(input.data(),input.size());return state.Finish();
}
std::string StableMarkId(std::string_view reference_id,std::size_t slot) {
  const std::string seed=std::string(reference_id)+":"+std::to_string(slot);
  return Sha256(Bytes(seed.begin(),seed.end())).substr(0,32);
}
bool ValidPageIdentity(const PageIdentity& identity, std::string* error) {
  auto fail=[&](const char* message){if(error)*error=message;return false;};
  const auto hex=[](const std::string& value) {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c) {
      return (c>='0'&&c<='9')||(c>='a'&&c<='f');
    });
  };
  if (identity.provider!="chrome" ||
      (identity.provider_version!=1 && identity.provider_version!=2) ||
      identity.bundle_id!="com.google.Chrome" || identity.browser_pid<=0 ||
      identity.window_id==0 || !ValidRect(identity.window_bounds) ||
      identity.process_start_identity_us<=0 ||
      identity.opaque_tab_id.empty() || identity.opaque_tab_id.size()>512 ||
      identity.navigation_digest.empty() || !hex(identity.navigation_digest))
    return fail("invalid Chrome page identity");
  return true;
}
bool SamePageIdentity(const PageIdentity& left, const PageIdentity& right) {
  return left.provider==right.provider &&
      left.provider_version==right.provider_version &&
      left.bundle_id==right.bundle_id && left.browser_pid==right.browser_pid &&
      left.window_id==right.window_id && Same(left.window_bounds,right.window_bounds) &&
      left.process_start_identity_us==right.process_start_identity_us &&
      left.opaque_tab_id==right.opaque_tab_id &&
      left.navigation_digest==right.navigation_digest;
}
bool PageIdentityMatchesContext(const PageIdentity& identity,
                                const Context& context) {
  return identity.provider=="chrome" && context.bundle_id==identity.bundle_id &&
      identity.browser_pid==context.window_pid && identity.window_id==context.window_id &&
      Same(identity.window_bounds,context.window) &&
      (context.native_window.version==0 ||
       (identity.provider_version==2 && context.native_window.version==1 &&
        identity.process_start_identity_us==context.native_window.process_start_identity_us));
}
bool PageIdentityVisible(const std::optional<PageIdentity>& required,
                         const PageObservation& current) {
  // A page-scoped record is visible only after a fresh, matching observation.
  if (!required) return true;
  const auto now=Now();
  const auto age=now.monotonic_us-current.observed.monotonic_us;
  return current.availability==PageAvailability::kReady &&
      current.identity && SamePageIdentity(*required,*current.identity) &&
      current.observed.monotonic_us>0 && age>=0 && age<=2'000'000;
}
void NativeWindowLifetimeTracker::Observe(std::int64_t pid,std::int64_t start,
                                           const std::vector<std::uint64_t>& ids,
                                           std::uint64_t generation,bool complete) {
  if(!complete || generation<=generation_ || pid<=0 || start<=0 || session_.empty())return;
  generation_=generation;
  for(auto& entry:entries_)if(entry.pid==pid && entry.start==start)
    entry.present=std::find(ids.begin(),ids.end(),entry.id)!=ids.end() && entry.present;
  for(const auto id:ids) {
    if(!id)continue;
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry) {
      return entry.pid==pid && entry.start==start && entry.id==id;
    });
    if(found==entries_.end())entries_.push_back({pid,start,id,++next_,true});
    else if(!found->present){found->present=true;found->incarnation=++next_;}
  }
}
NativeWindowIdentity NativeWindowLifetimeTracker::Identity(
    std::int64_t pid,std::int64_t start,std::uint64_t id) const {
  for(const auto& entry:entries_)
    if(entry.pid==pid && entry.start==start && entry.id==id && entry.present)
      return {1,start,session_,entry.incarnation};
  return {1,start,{},0};
}
namespace {
bool NativeIdentityMatches(const NativeWindowIdentity& required,
                           const NativeWindowIdentity& current) {
  // Preserve the original weaker fresh-tuple branch for genuine old records.
  if(required.version==0)return required.process_start_identity_us==0 &&
      required.observation_session.empty() && required.incarnation==0;
  if((required.version!=1&&required.version!=2) || current.version!=required.version ||
     required.process_start_identity_us<=0 || current.process_start_identity_us<=0 ||
     required.process_start_identity_us!=current.process_start_identity_us ||
     required.observation_session.empty() || current.observation_session.empty() ||
     !required.incarnation || !current.incarnation)return false;
  // A fresh SeeThis process cannot reconstruct unobserved historical windows.
  return required.observation_session!=current.observation_session ||
      required.incarnation==current.incarnation;
}
}
bool ValidCaptureScope(const Context& c) {
  const auto& scope=c.scope;
  if(!scope.policy_present)return scope.version==0 && scope.kind==CaptureScopeKind::kLegacy &&
      scope.display_id==0 && scope.process_start_identity_us==0 && !scope.title_required &&
      scope.process_source==ProcessStartSource::kUnspecified && c.native_window.version!=2;
  const bool kernel=scope.version==2&&scope.process_source==ProcessStartSource::kKernelBSDStart;
  if((!kernel&&(scope.version!=1||scope.process_source!=ProcessStartSource::kUnspecified)) ||
     (kernel&&scope.kind==CaptureScopeKind::kPage) || scope.process_start_identity_us<=0 ||
     scope.process_start_identity_us>c.observed.utc_us || c.pid<=0 || c.bundle_id.empty() ||
     std::none_of(c.displays.begin(),c.displays.end(),[&](const auto& d){return d.id==scope.display_id;}))return false;
  if(scope.kind==CaptureScopeKind::kApplication)
    return !scope.title_required && c.window_id==0 && c.window_pid==0 && c.window_title.empty() &&
        c.selection_method=="frontmost-application/selected-display";
  return (!scope.title_required || (scope.kind==CaptureScopeKind::kWindow && c.bundle_id!="com.google.Chrome" &&
      c.title.version==1 && c.title.availability==TitleAvailability::kReady && ValidSourceContextText(c.window_title,1024))) &&
      (scope.kind==CaptureScopeKind::kPage || scope.kind==CaptureScopeKind::kWindow) &&
      c.window_id>0 && c.window_pid==c.pid && c.native_window.version==(kernel?2:1) &&
      !c.native_window.observation_session.empty() && c.native_window.incarnation>0 &&
      c.native_window.process_start_identity_us==scope.process_start_identity_us;
}
std::string WindowSourceKey(std::int64_t pid,std::uint64_t window) {
  return std::to_string(pid)+":"+std::to_string(window);
}
std::string ApplicationSourceKey(std::int64_t pid,std::int64_t start,DisplayId display,ProcessStartSource source) {
  return std::to_string(static_cast<unsigned>(source))+":"+std::to_string(pid)+":"+std::to_string(start)+":"+std::to_string(display);
}
ProcessStartSource CaptureProcessSource(const Context& context) {
  if(context.scope.version==2)return context.scope.process_source;
  if(context.scope.version==1||context.native_window.version==1)return ProcessStartSource::kLaunchDate;
  return ProcessStartSource::kUnspecified;
}
const char* ProcessStartSourceName(ProcessStartSource source) {
  switch(source) {
    case ProcessStartSource::kLaunchDate:return "launch-services-date-v1";
    case ProcessStartSource::kKernelBSDStart:return "kernel-bsd-start-v1";
    default:return "legacy-unspecified";
  }
}
const NativeWindowIdentity& WindowIdentityForContext(const Context& context,const WindowObservation& observation) {
  return CaptureProcessSource(context)==ProcessStartSource::kKernelBSDStart?
      observation.kernel_native_window:observation.native_window;
}
std::int64_t ApplicationIdentityForContext(const Context& context,const WindowObservation& observation) {
  return CaptureProcessSource(context)==ProcessStartSource::kKernelBSDStart?
      observation.kernel_application_start_identity_us:observation.application_start_identity_us;
}
const char* CaptureScopeKindName(CaptureScopeKind kind) {
  switch(kind) {
    case CaptureScopeKind::kLegacy:return "legacy";
    case CaptureScopeKind::kPage:return "page";
    case CaptureScopeKind::kWindow:return "window";
    case CaptureScopeKind::kApplication:return "application";
  }
  return "unsupported";
}
const char* CaptureScopeName(const Context& c) {
  if(!ValidCaptureScope(c))return "Unavailable scope";
  switch(c.scope.kind) {
    case CaptureScopeKind::kPage:return "Follows this page";
    case CaptureScopeKind::kWindow:return "Follows this window";
    case CaptureScopeKind::kApplication:return "Follows this app (fixed screen positions across its windows)";
    default:return "Legacy source";
  }
}
bool WindowObservationMatchesContext(const Context& required,
                                     const WindowObservation& current) {
  const auto now=Now();
  const auto age=now.monotonic_us-current.observed.monotonic_us;
  if(!ValidCaptureScope(required))return false;
  if(required.scope.kind==CaptureScopeKind::kApplication) {
    const auto stamp=current.application_observed.monotonic_us?current.application_observed:current.observed;
    const auto app_age=now.monotonic_us-stamp.monotonic_us;
    return current.application_available && current.generation>0 &&
        stamp.monotonic_us>0 && app_age>=0 && app_age<=2'000'000 &&
        current.pid==required.pid && current.bundle_id==required.bundle_id &&
        ApplicationIdentityForContext(required,current)==required.scope.process_start_identity_us;
  }
  const auto& native=WindowIdentityForContext(required,current);
  return NativeIdentityMatches(required.native_window,native) &&
      (required.native_window.version==0 ||
       (required.pid==required.window_pid && required.window_pid>0 &&
        required.window_id>0 && !required.bundle_id.empty() && ValidRect(required.window) &&
        required.native_window.process_start_identity_us<=required.observed.utc_us &&
        native.process_start_identity_us<=current.observed.utc_us)) &&
      current.availability==WindowAvailability::kReady &&
      current.generation>0 && current.observed.monotonic_us>0 &&
      age>=0 && age<=2'000'000 && current.pid==required.window_pid &&
      current.bundle_id==required.bundle_id &&
      current.window_id==required.window_id &&
      Same(current.bounds,required.window);
}
namespace {
bool CaptureContextProvenanceSupported(const Context& c) {
  // These are the capture-origin fields already frozen in positional Context.
  // Genuine native-v0 records retain their weaker tuple lifetime semantics.
  std::set<std::string> uuids;std::set<DisplayId> ids;
  for(const auto& d:c.displays) {
    if(d.uuid.empty() || !uuids.insert(d.uuid).second || !ids.insert(d.id).second || !d.id ||
       !ValidRect(d.logical) || !ValidRect(d.pixels) || !Finite(d.scale) || d.scale<=0 ||
       d.pixels.width!=d.logical.width*d.scale || d.pixels.height!=d.logical.height*d.scale ||
       !Same(d.logical_to_pixels,{d.scale,-d.scale,d.pixels.x-d.logical.x*d.scale,
           d.pixels.y+(d.logical.y+d.logical.height)*d.scale}))return false;
  }
  if(!ValidCaptureScope(c))return false;
  if(c.scope.kind==CaptureScopeKind::kApplication)
    return c.self_pid>0 && c.pid!=c.self_pid && !c.app_name.empty() &&
        ValidStamp(c.observed) && !c.displays.empty() &&
        Finite(c.quartz_to_appkit_top) && c.quartz_to_appkit_top>0 &&
        c.exclusion_method=="exact-window-allowlist;self-pid-rejected;child-windows-off" &&
        !c.excluded_window_ids.empty();
  return c.pid>0 && c.window_pid==c.pid && c.self_pid>0 && c.self_pid!=c.pid &&
      c.window_id>0 && c.window_layer==0 && ValidRect(c.window) &&
      Finite(c.quartz_to_appkit_top) && c.quartz_to_appkit_top>0 &&
      !c.app_name.empty() && (!c.bundle_id.empty() || !c.executable.empty()) &&
      ValidStamp(c.observed) && !c.displays.empty() &&
      c.selection_method=="frontmost-pid/topmost-normal-containing-initial-point" &&
      c.exclusion_method=="exact-window-allowlist;self-pid-rejected;child-windows-off" &&
      !c.excluded_window_ids.empty() &&
      std::find(c.excluded_window_ids.begin(),c.excluded_window_ids.end(),c.window_id)==
          c.excluded_window_ids.end();
}
bool LegacyCaptureTitleSupported(const Context& c) {
  return CaptureContextProvenanceSupported(c) && ValidSourceContextText(c.window_title,1024);
}
}
const char* TitleScopeCapabilityName(const Context& c) {
  if(c.scope.policy_present) {
    if(!ValidCaptureScope(c))return "Unavailable capture scope; overlays hidden";
    if(c.scope.kind==CaptureScopeKind::kApplication)return CaptureScopeName(c);
    if(c.scope.kind==CaptureScopeKind::kWindow&&!c.scope.title_required)
      return "Follows this window; no title constraint was available when captured";
  }
  if(c.bundle_id=="com.google.Chrome")return "Chrome page identity; title not applicable";
  if(c.title.version==0) {
    if(c.title.policy_present || c.title.availability!=TitleAvailability::kUnavailable || !LegacyCaptureTitleSupported(c))
      return "Legacy title unavailable: missing or invalid capture evidence; recapture required";
    return c.native_window.version==0?
        "Legacy captured exact title; weaker native tuple lifetime (no process-start evidence)":
        "Legacy captured exact title with native process identity";
  }
  if(c.title.version!=1)return "Unsupported title policy; overlays hidden";
  if(!CapturedTitleAvailable(c))
    return "Title-scoped-v1 unavailable; overlays hidden; history retained";
  return c.native_window.version==0?
      "Title-scoped-v1 exact title; weaker native tuple lifetime (no process-start evidence)":
      "Title-scoped-v1 exact title heuristic (1024 UTF-8 bytes maximum)";
}
bool CapturedTitleAvailable(const Context& required) {
  return required.title.version==0?
      !required.title.policy_present && required.title.availability==TitleAvailability::kUnavailable && LegacyCaptureTitleSupported(required):
      required.title.version==1 && required.title.availability==TitleAvailability::kReady &&
          CaptureContextProvenanceSupported(required) && ValidSourceContextText(required.window_title,1024);
}
bool TitleContextVisible(const Context& required,const WindowObservation& current) {
  // WindowObservationMatchesContext supplies the shared fresh timestamp/native
  // gate. Never match empty/unavailable strings or guess a historical title.
  return CapturedTitleAvailable(required) && current.title.version==1 &&
      current.title.availability==TitleAvailability::kReady &&
      ValidSourceContextText(current.window_title,1024) &&
      required.window_title==current.window_title;
}
bool ReferenceVisible(const Context& context,
                      const std::optional<PageIdentity>& page_identity,
                      const WindowObservation& window_observation,
                      const PageObservation& page_observation) {
  if(!WindowObservationMatchesContext(context,window_observation))return false;
  if(context.scope.kind==CaptureScopeKind::kApplication)return !page_identity;
  if(context.scope.kind==CaptureScopeKind::kWindow)
    return !page_identity && (!context.scope.title_required || TitleContextVisible(context,window_observation));
  if(context.scope.kind==CaptureScopeKind::kPage && !page_identity)return false;
  if(!page_identity)return context.bundle_id!="com.google.Chrome" &&
      TitleContextVisible(context,window_observation);
  return PageIdentityMatchesContext(*page_identity,context) &&
      PageIdentityVisible(page_identity,page_observation);
}
bool ValidPixels(const Pixels& p) {
  return p.width>0 && p.height>0 && p.width<=16384 && p.height<=16384 &&
    std::uint64_t(p.width)*p.height*4<=128ULL*1024*1024 && p.rgba.size()==std::uint64_t(p.width)*p.height*4;
}
Bytes EncodeSdkPng(const Pixels& p) {
  if(!ValidPixels(p)) return {};
  Bytes out={137,80,78,71,13,10,26,10},ihdr;BE32(ihdr,p.width);BE32(ihdr,p.height);
  ihdr.insert(ihdr.end(),{8,6,0,0,0});Chunk(out,"IHDR",ihdr);Chunk(out,"sRGB",{0});
  z_stream stream{};if(deflateInit(&stream,Z_DEFAULT_COMPRESSION)!=Z_OK)return {};
  std::array<std::uint8_t,64*1024> output{};bool ok=true;
  const auto pump=[&](const std::uint8_t* input,std::size_t size,int flush) {
    stream.next_in=const_cast<Bytef*>(input);stream.avail_in=static_cast<uInt>(size);
    int status=Z_OK;
    do {
      stream.next_out=output.data();stream.avail_out=output.size();
      status=deflate(&stream,flush);
      if(status!=Z_OK&&status!=Z_STREAM_END)return false;
      const auto produced=output.size()-stream.avail_out;
      if(produced>0)Chunk(out,"IDAT",Bytes(output.begin(),output.begin()+produced));
    } while(stream.avail_in>0||(flush==Z_FINISH&&status!=Z_STREAM_END));
    return flush==Z_FINISH?status==Z_STREAM_END:true;
  };
  static constexpr std::uint8_t filter=0;
  for(std::size_t row=0;row<p.height&&ok;++row) {
    ok=pump(&filter,1,Z_NO_FLUSH)&&
       pump(p.rgba.data()+row*std::size_t(p.width)*4,
            std::size_t(p.width)*4,Z_NO_FLUSH);
  }
  if(ok)ok=pump(nullptr,0,Z_FINISH);
  deflateEnd(&stream);
  if(!ok)return {};
  Chunk(out,"IEND",{});return out;
}
namespace {
Bytes EncodeLegacyPng(const Pixels& p) {
  if(!ValidPixels(p))return {};
  Bytes raw;raw.reserve(p.rgba.size()+p.height);
  for(std::size_t row=0;row<p.height;++row) {
    raw.push_back(0);const auto* begin=p.rgba.data()+row*std::size_t(p.width)*4;
    raw.insert(raw.end(),begin,begin+std::size_t(p.width)*4);
  }
  Bytes compressed={0x78,0x01};
  for(std::size_t offset=0;offset<raw.size();) {
    const auto size=std::min<std::size_t>(65535,raw.size()-offset);
    compressed.push_back(offset+size==raw.size()?1:0);
    compressed.push_back(size&255);compressed.push_back(size>>8);
    compressed.push_back((~size)&255);compressed.push_back(((~size)>>8)&255);
    compressed.insert(compressed.end(),raw.begin()+offset,
                      raw.begin()+offset+size);offset+=size;
  }
  auto adler=adler32(0L,Z_NULL,0);
  adler=adler32(adler,raw.data(),static_cast<uInt>(raw.size()));
  BE32(compressed,static_cast<std::uint32_t>(adler));
  Bytes out={137,80,78,71,13,10,26,10},ihdr;BE32(ihdr,p.width);BE32(ihdr,p.height);
  ihdr.insert(ihdr.end(),{8,6,0,0,0});Chunk(out,"IHDR",ihdr);Chunk(out,"sRGB",{0});
  Chunk(out,"IDAT",compressed);Chunk(out,"IEND",{});return out;
}
std::optional<std::string> RgbaHash(const Pixels& pixels,Rect bounds) {
  if(!ValidRect(bounds)||bounds.x<0||bounds.y<0||
     bounds.x+bounds.width>pixels.width||bounds.y+bounds.height>pixels.height)
    return {};
  const auto x=static_cast<std::size_t>(bounds.x),y=static_cast<std::size_t>(bounds.y);
  const auto width=static_cast<std::size_t>(bounds.width);
  const auto height=static_cast<std::size_t>(bounds.height);
  if(bounds.x!=x||bounds.y!=y||bounds.width!=width||bounds.height!=height)
    return {};
  Sha256State hash;hash.Update(std::string_view("seethis-rgba8-v1\0",17));
  std::array<std::uint8_t,8> dimensions{};
  for(int index=0;index<4;++index) {
    dimensions[static_cast<std::size_t>(3-index)]=
        static_cast<std::uint8_t>(width>>(index*8));
    dimensions[static_cast<std::size_t>(7-index)]=
        static_cast<std::uint8_t>(height>>(index*8));
  }
  hash.Update(dimensions.data(),dimensions.size());
  for(std::size_t row=0;row<height;++row)
    hash.Update(pixels.rgba.data()+((y+row)*pixels.width+x)*4,width*4);
  return hash.Finish();
}
}
Bytes EncodePng(const Pixels& pixels) {return EncodeLegacyPng(pixels);}
bool ValidSourceContextText(std::string_view value,std::size_t maximum_bytes) {
  if(value.empty()||value.size()>maximum_bytes)return false;
  for(std::size_t i=0;i<value.size();) {
    const auto first=static_cast<unsigned char>(value[i++]);
    if(first<0x20||first==0x7f)return false;
    if(first<0x80)continue;
    unsigned count=0;std::uint32_t code=0,minimum=0;
    if(first>=0xc2&&first<=0xdf){count=1;code=first&0x1f;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){count=2;code=first&0x0f;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){count=3;code=first&7;minimum=0x10000;}
    else return false;
    if(count>value.size()-i)return false;
    while(count--) {
      const auto next=static_cast<unsigned char>(value[i++]);
      if((next&0xc0)!=0x80)return false;
      code=(code<<6)|(next&0x3f);
    }
    if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff)||
       (code>=0x80&&code<=0x9f))return false;
  }
  return true;
}
const char* SourceFieldAvailabilityName(SourceFieldAvailability availability) {
  switch(availability) {
    case SourceFieldAvailability::kCaptured:return "captured";
    case SourceFieldAvailability::kLegacyUnavailable:return "legacy_unavailable";
    case SourceFieldAvailability::kNotApplicable:return "not_applicable";
    case SourceFieldAvailability::kInvalidOrOverLimit:return "invalid_or_over_limit";
  }
  return "invalid_or_over_limit";
}
namespace {
bool ValidCapturedPageMetadata(const CapturedPageMetadata& metadata) {
  if(metadata.availability==SourceFieldAvailability::kCaptured)
    return metadata.url&&ValidSourceContextText(*metadata.url,16384)&&
        metadata.observed_utc_us&&*metadata.observed_utc_us>0;
  return (metadata.availability==SourceFieldAvailability::kLegacyUnavailable||
      metadata.availability==SourceFieldAvailability::kNotApplicable||
      metadata.availability==SourceFieldAvailability::kInvalidOrOverLimit)&&
      !metadata.url&&!metadata.observed_utc_us;
}
bool CapturedPageMatches(const CapturedPageMetadata& metadata,
                         const std::optional<PageIdentity>& identity,const Context& context) {
  if(!ValidCapturedPageMetadata(metadata))return false;
  if(metadata.availability!=SourceFieldAvailability::kCaptured)return true;
  if(!identity||!ValidPageIdentity(*identity)||
     !PageIdentityMatchesContext(*identity,context))return false;
  static constexpr std::string_view domain="seethis/chrome-navigation/v1\0";
  Bytes input(domain.begin(),domain.end());
  input.insert(input.end(),metadata.url->begin(),metadata.url->end());
  return Sha256(input)==identity->navigation_digest;
}
void NormalizeCapturedPage(Reference& reference) {
  if(reference.captured_page &&
     !CapturedPageMatches(*reference.captured_page,reference.page_identity,reference.context)) {
    reference.captured_page=CapturedPageMetadata{std::nullopt,
        SourceFieldAvailability::kInvalidOrOverLimit,std::nullopt};
  }
}
void WriteCapturedPage(Writer& writer,const std::optional<CapturedPageMetadata>& source) {
  const std::uint64_t present=source?1:0;writer(present);
  if(!source)return;
  auto metadata=*source;
  const std::uint64_t has_url=metadata.url?1:0,has_observed=metadata.observed_utc_us?1:0;
  writer(metadata.availability,has_url);
  if(has_url)writer(*metadata.url);
  writer(has_observed);
  if(has_observed)writer(*metadata.observed_utc_us);
}
bool ReadCapturedPage(Reader& reader,std::optional<CapturedPageMetadata>& target) {
  std::uint64_t present=0;reader(present);if(present>1)return false;
  if(!present){target.reset();return true;}
  CapturedPageMetadata metadata;std::uint64_t status=0,has_url=0,has_observed=0;
  reader(status,has_url);if(status>3||has_url>1)return false;
  metadata.availability=static_cast<SourceFieldAvailability>(status);
  if(has_url){std::string url;reader(url);metadata.url=std::move(url);}
  reader(has_observed);if(has_observed>1)return false;
  if(has_observed){std::int64_t observed=0;reader(observed);metadata.observed_utc_us=observed;}
  if(!ValidCapturedPageMetadata(metadata))return false;
  target=std::move(metadata);return true;
}
void ProjectText(std::string_view value,std::size_t limit,
                 std::optional<std::string>& output,SourceFieldAvailability& availability) {
  availability=value.empty()?SourceFieldAvailability::kLegacyUnavailable:
      SourceFieldAvailability::kInvalidOrOverLimit;
  if(ValidSourceContextText(value,limit)) {
    output=std::string(value);availability=SourceFieldAvailability::kCaptured;
  }
}
}
SourceContextProjection ProjectSourceContext(const Reference& reference) {
  SourceContextProjection result;const auto& context=reference.context;
  result.scope=context.scope;result.scope_label=CaptureScopeName(context);result.process_source=CaptureProcessSource(context);
  ProjectText(context.app_name,256,result.app_name,result.app_name_availability);
  ProjectText(context.bundle_id,512,result.bundle_id,result.bundle_id_availability);
  ProjectText(context.window_title,1024,result.window_title,result.window_title_availability);
  const bool chrome=context.bundle_id=="com.google.Chrome";
  if((chrome||context.scope.kind==CaptureScopeKind::kApplication)&&context.window_title.empty())
    result.window_title_availability=SourceFieldAvailability::kNotApplicable;
  if(context.window_id>0)result.window_id=context.window_id;
  if(ValidStamp(context.observed))result.context_observed_utc_us=context.observed.utc_us;
  if(ValidStamp(reference.image_completed))
    result.image_completed_utc_us=reference.image_completed.utc_us;
  if(!chrome||(context.scope.policy_present&&context.scope.kind!=CaptureScopeKind::kPage))result.page_url_availability=SourceFieldAvailability::kNotApplicable;
  else if(reference.captured_page) {
    const auto& metadata=*reference.captured_page;
    if(CapturedPageMatches(metadata,reference.page_identity,reference.context)) {
      result.page_url=metadata.url;result.page_url_availability=metadata.availability;
      result.page_url_observed_utc_us=metadata.observed_utc_us;
    } else result.page_url_availability=SourceFieldAvailability::kInvalidOrOverLimit;
  }
  return result;
}
SourceContextProjection ProjectSourceContext(const ReferenceJobSnapshot& job) {
  SourceContextProjection result;
  if(job.metadata)result=ProjectSourceContext(*job.metadata);
  else if(job.ready)result=ProjectSourceContext(job.ready->value);
  else {
    Reference reference;reference.context=job.context;
    reference.page_identity=job.page_identity;reference.captured_page=job.captured_page;
    result=ProjectSourceContext(reference);
  }
  if(job.state!=ReferenceJobState::kReady)result.image_completed_utc_us.reset();
  return result;
}

Bytes Serialize(const Reference& reference) {
  Reference copy=reference;NormalizeCapturedPage(copy); Writer w; w(copy);
  if (copy.schema >= 2 && copy.subpath_version >= 1) {
    const std::uint64_t version = 1;
    w(version);
    for (auto& region : copy.regions) w(region.subpaths);
    w(copy.source.encoding, copy.crop.encoding, copy.crop.pixel_hash_contract,
      copy.crop.pixel_sha256);
  }
  if (copy.page_identity) {
    const std::uint64_t page_version = 2;
    const bool has_page_identity = true;
    w(page_version, has_page_identity, *copy.page_identity);
  }
  if(copy.captured_page) {
    const std::uint64_t metadata_version=3;w(metadata_version);
    WriteCapturedPage(w,copy.captured_page);
  }
  if(copy.context.native_window.version) {
    const std::uint64_t native_version=4;w(native_version);
    auto& identity=copy.context.native_window;
    w(identity.version,identity.process_start_identity_us,
      identity.observation_session,identity.incarnation);
  }
  if(copy.context.title.policy_present || copy.context.title.version || copy.context.title.availability!=TitleAvailability::kUnavailable) {
    const std::uint64_t title_tail=5;w(title_tail);
    w(copy.context.title.version,copy.context.title.availability);
  }
  if(copy.context.scope.policy_present) {
    const std::uint64_t scope_tail=6;const auto& scope=copy.context.scope;
    w(scope_tail,scope.version,scope.kind,scope.display_id,scope.process_start_identity_us,scope.title_required);
    if(scope.version==2)w(scope.process_source);
  }
  return w.bytes;
}
std::optional<Reference> Deserialize(const Bytes& bytes) {
  if(bytes.size()>kMaxBytes) return {};
  try { Reference v; Reader r{bytes}; bool explicit_tail = false; r(v);
    std::uint64_t next_version=0;
    if(v.schema>=2&&r.offset<bytes.size()) {
      r(next_version);
      if(next_version==1) {
        v.subpath_version=1;explicit_tail=true;
        for(auto& region:v.regions)r(region.subpaths);
        r(v.source.encoding,v.crop.encoding,v.crop.pixel_hash_contract,v.crop.pixel_sha256);
        next_version=0;
      }
    }
    if(next_version==0&&r.offset<bytes.size())r(next_version);
    if(next_version==2) {
      std::uint64_t present=0;r(present);if(present!=1)return {};
      PageIdentity identity;r(identity);v.page_identity=std::move(identity);
      next_version=0;
      if(r.offset<bytes.size())r(next_version);
    }
    if(next_version==3) {
      if(!ReadCapturedPage(r,v.captured_page)||!v.captured_page)return {};
      next_version=0;
      if(r.offset<bytes.size())r(next_version);
    }
    if(next_version==4) {
      auto& identity=v.context.native_window;
      r(identity.version,identity.process_start_identity_us,
        identity.observation_session,identity.incarnation);
      if((identity.version!=1&&identity.version!=2) || identity.observation_session.size()>128)return {};
      next_version=0;
      if(r.offset<bytes.size())r(next_version);
    }
    if(next_version==5) {
      // Preserve unsupported versions/statuses as unavailable evidence. Dropping
      // this tail would silently turn a required record into a legacy match.
      r(v.context.title.version,v.context.title.availability);
      v.context.title.policy_present=true;
      next_version=0;
      if(r.offset<bytes.size())r(next_version);
    }
    if(next_version==6) {
      auto& scope=v.context.scope;
      r(scope.version,scope.kind,scope.display_id,scope.process_start_identity_us,scope.title_required);
      if(scope.version==2)r(scope.process_source);
      scope.policy_present=true;next_version=0;
    }
    if(next_version!=0)return {};
    if(r.offset!=bytes.size() || Serialize(v)!=bytes)return {};
    for(auto* image:{&v.source,&v.crop}) {
      // The legacy record layout has no codec field. Treat it as legacy-only;
      // malformed/noncanonical stored-block bytes must never be reaccepted by
      // the broader SDK-zlib validator.
      if (v.schema == 1 || (v.schema == 2 && !explicit_tail)) {
        image->encoding="png-zlib-stored-v1";
        image->pixel_hash_contract.clear();image->pixel_sha256.clear();
      }
    }
    return v; }
  catch(const std::exception&) { return {}; }
}
std::vector<ReferenceRegion> EffectiveRegions(const Reference& reference) {
  if(!reference.regions.empty())return reference.regions;
  if(reference.path.empty())return {};
  return {{reference.path,reference.region,reference.crop_pixels,{}}};
}
StoredReference Canonical(const Reference& reference) {
  StoredReference out;out.value=reference;
  if(reference.schema==1)out.record=Serialize(reference);
  const std::string text=reference.schema==1?
    "{\"schema\":1,\"reference_id\":\""+reference.id+
    "\",\"mark_id\":\""+reference.mark_id+"\",\"historical\":true,\"encoding\":\"hex-stref1\",\"record\":\""+Hex(out.record)+"\"}\n":
    "{\"schema\":2,\"reference_id\":\""+reference.id+
    "\",\"mark_id\":\""+reference.mark_id+"\",\"historical\":true,\"encoding\":\"local-stref2\"}\n";
  out.clipboard_text.assign(text.begin(),text.end());
  if(reference.schema==1)out.clipboard_png=reference.crop.bytes;
  return out;
}
std::optional<Rect> CropBounds(const Context& c,const std::vector<DisplayPoint>& path,
                              std::uint32_t width,std::uint32_t height,double margin) {
  if(!ValidRect(c.window) || path.empty() || width==0 || height==0 || !Finite(margin) || margin<0) return {};
  double minx=std::numeric_limits<double>::infinity(),miny=minx,maxx=-minx,maxy=-minx;
  for(const auto& p:path) {
    const auto d=std::find_if(c.displays.begin(),c.displays.end(),[&](const auto& d){return d.id==p.display_id;});
    if(d==c.displays.end() || !p.IsValid() || p.unit!=CoordinateUnit::kLogicalPoints ||
       p.backing_scale!=d->scale || p.position.x<0 || p.position.y<0 ||
       p.position.x>d->logical.width || p.position.y>d->logical.height) return {};
    const double x=d->logical.x+p.position.x,y=d->logical.y+p.position.y;
    minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
  }
  const double sx=width/c.window.width,sy=height/c.window.height;
  const double x=std::clamp(std::floor((minx-margin-c.window.x)*sx),0.0,double(width));
  const double y=std::clamp(std::floor((c.window.y+c.window.height-maxy-margin)*sy),0.0,double(height));
  const double right=std::clamp(std::ceil((maxx+margin-c.window.x)*sx),0.0,double(width));
  const double bottom=std::clamp(std::ceil((c.window.y+c.window.height-miny+margin)*sy),0.0,double(height));
  Rect r{x,y,right-x,bottom-y};if(!ValidRect(r))return {};return r;
}
bool DisplayTopologyMatches(const std::vector<FrozenDisplay>& frozen,const std::vector<FrozenDisplay>& current) {
  if(frozen.empty() || frozen.size()!=current.size())return false;
  for(const auto& d:frozen) {
    auto it=std::find_if(current.begin(),current.end(),[&](const auto& n){return n.uuid==d.uuid;});
    if(it==current.end() || it->id!=d.id || !Same(d.logical,it->logical) ||
       !Same(d.pixels,it->pixels) || d.scale!=it->scale || !Same(d.logical_to_pixels,it->logical_to_pixels))return false;
  }return true;
}
bool Validate(const Reference& r,std::string* error) {
  auto fail=[&](const char* message){if(error)*error=message;return false;};
  if((r.schema!=1&&r.schema!=2) || !SafeID(r.id) || !SafeID(r.mark_id))return fail("invalid reference identity");
  if (r.page_identity) {
    if (r.schema < 2 || !ValidPageIdentity(*r.page_identity) ||
        !PageIdentityMatchesContext(*r.page_identity,r.context))
      return fail("invalid page identity");
  }
  const auto& c=r.context;
  if(!CaptureContextProvenanceSupported(c) ||
     ((c.scope.kind==CaptureScopeKind::kPage)!=bool(r.page_identity) && c.scope.policy_present))
    return fail("invalid context/provenance or scope");
  std::set<std::string> uuids;std::set<DisplayId> ids;
  for(const auto& d:c.displays) {
    if(d.uuid.empty() || !uuids.insert(d.uuid).second || !ids.insert(d.id).second || d.id==0 ||
       !ValidRect(d.logical) || !ValidRect(d.pixels) || !Finite(d.scale) || d.scale<=0 ||
       d.pixels.width!=d.logical.width*d.scale || d.pixels.height!=d.logical.height*d.scale ||
       !Same(d.logical_to_pixels,{d.scale,-d.scale,d.pixels.x-d.logical.x*d.scale,
         d.pixels.y+(d.logical.y+d.logical.height)*d.scale}))return fail("invalid frozen display");
  }
  if(c.displays.empty() || !ValidStamp(c.observed) || !ValidStamp(r.requested) || !ValidStamp(r.image_completed) ||
     !ValidStamp(r.circle_completed) || r.requested.monotonic_us<c.observed.monotonic_us ||
     r.image_completed.monotonic_us<r.requested.monotonic_us || r.circle_completed.monotonic_us<c.observed.monotonic_us ||
     r.clock!="UTC:system_clock/us;monotonic:steady_clock/us;process-session" ||
     (r.capture_api!="ScreenCaptureKit.SCScreenshotManager.exact-window"&&
      r.capture_api!="ScreenCaptureKit.SCScreenshotManager.selected-display") ||
     (r.provenance!="historical;context-before-overlay;pixels-at-async-completion;no-semantic-object-id"&&
      r.provenance!="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id"))return fail("invalid phase timestamps");
  const auto descriptor_ok=[](const Image& i) {
    return i.width>0 && i.height>0 && i.width<=16384 && i.height<=16384 &&
      std::uint64_t(i.width)*i.height*4<=128ULL*1024*1024 &&
      i.format=="image/png"&&i.color_space=="sRGB"&&
      (i.encoding=="png-zlib-stored-v1"||i.encoding=="png-zlib-v1");
  };
  const auto byte_image_ok=[&](const Image& image) {
    return descriptor_ok(image)&&image.bytes.size()>=33&&image.bytes[0]==137&&
        image.bytes[1]==80&&image.bytes[2]==78&&image.bytes[3]==71&&
        image.sha256.size()==64&&image.sha256==Sha256(image.bytes)&&
        image.pixel_hash_contract.empty()&&image.pixel_sha256.empty();
  };
  const bool demand_crop=descriptor_ok(r.crop)&&r.crop.sha256.empty()&&
      r.crop.pixel_hash_contract=="rgba8-sha256-v1"&&
      r.crop.pixel_sha256.size()==64;
  if(!byte_image_ok(r.source)||(!demand_crop&&!byte_image_ok(r.crop)))
    return fail("invalid image/hash contract");
  if (r.schema == 1 && r.subpath_version != 0)
    return fail("legacy reference has subpath encoding");
  if (r.schema >= 2 && r.subpath_version > 1)
    return fail("unknown subpath encoding");
  const auto regions=EffectiveRegions(r);
  if(regions.empty())return fail("missing regions");
  Context geometry=c;
  if(r.schema==2) {
    const auto first_subpath = [&]() -> const std::vector<DisplayPoint>* {
      for (const auto& region : regions) {
        if (!region.subpaths.empty()) {
          if (!region.subpaths.front().empty()) return &region.subpaths.front();
        } else if (!region.path.empty()) return &region.path;
      }
      return nullptr;
    }();
    if (!first_subpath) return fail("missing selected display");
    const auto display_id=first_subpath->front().display_id;
    const auto display=std::find_if(c.displays.begin(),c.displays.end(),
      [&](const auto& value){return value.id==display_id;});
    if(display==c.displays.end()||display->pixels.width!=r.source.width||
       display->pixels.height!=r.source.height)return fail("selected display mismatch");
    geometry.window=display->logical;
  }
  std::vector<DisplayPoint> all_path;
  for(const auto& item:regions) {
    bool has_path = false;
    ForEachSubpath(item, [&](const auto& path) {
      if (path.size() >= 2) has_path = true;
      all_path.insert(all_path.end(), path.begin(), path.end());
    });
    if(!has_path)return fail("degenerate region");
    if (r.subpath_version == 1 && (!item.path.empty() || item.subpaths.empty()))
      return fail("invalid subpath representation");
    if (r.subpath_version == 1)
      for (const auto& path : item.subpaths)
        if (path.size() < 2) return fail("degenerate subpath");
  }
  auto crop=CropBounds(geometry,r.schema==1?r.path:all_path,r.source.width,r.source.height,r.crop_margin);
  if(!crop || !Same(*crop,r.crop_pixels) || r.crop.width!=crop->width || r.crop.height!=crop->height)return fail("invalid crop");
  if(r.source.encoding=="png-zlib-stored-v1"&&!DecodeLegacyPng(r.source))
    return fail("invalid legacy source PNG");
  const auto source_crop=InspectPng(r.source,*crop,false);
  if(!source_crop)return fail("invalid source PNG");
  if(demand_crop) {
    if(r.schema!=2||r.crop.encoding!="png-zlib-v1"||
       source_crop->rgba_sha256!=r.crop.pixel_sha256)
      return fail("derived crop pixel hash differs from source");
    if(!r.crop.bytes.empty()) {
      Image encoded=r.crop;encoded.sha256=Sha256(encoded.bytes);
      encoded.pixel_hash_contract.clear();encoded.pixel_sha256.clear();
      const auto derived=InspectPng(encoded,{0,0,double(r.crop.width),
                                            double(r.crop.height)},false);
      if(!derived||derived->rgba_sha256!=r.crop.pixel_sha256)
        return fail("derived crop PNG differs from source");
    }
  } else {
    if(r.crop.encoding=="png-zlib-stored-v1"&&!DecodeLegacyPng(r.crop))
      return fail("invalid legacy crop PNG");
    const auto crop_pixels=InspectPng(r.crop,{0,0,double(r.crop.width),
                                              double(r.crop.height)},false);
    if(!crop_pixels||crop_pixels->rgba_sha256!=source_crop->rgba_sha256)
      return fail("crop pixels differ from source");
  }
  const double sx=r.source.width/geometry.window.width,sy=r.source.height/geometry.window.height;
  if(!Same(r.global_to_source,{sx,-sy,-geometry.window.x*sx,(geometry.window.y+geometry.window.height)*sy}))return fail("invalid crop transform");
  Rect region{geometry.window.x+crop->x/sx,geometry.window.y+geometry.window.height-(crop->y+crop->height)/sy,crop->width/sx,crop->height/sy};
  if(!Same(region,r.region))return fail("invalid region");
  if(r.schema==1&&(!r.regions.empty()))return fail("legacy record contains regions");
  if(r.schema==2) {
    if(!r.path.empty())return fail("multi-region record contains flattened path");
    for(const auto& item:regions) {
      std::vector<DisplayPoint> item_path;
      ForEachSubpath(item, [&](const auto& path) {
        item_path.insert(item_path.end(), path.begin(), path.end());
      });
      auto item_crop=CropBounds(geometry,item_path,r.source.width,r.source.height,r.crop_margin);
      if(!item_crop||!Same(*item_crop,item.crop_pixels))return fail("invalid region crop");
      Rect item_region{geometry.window.x+item_crop->x/sx,
        geometry.window.y+geometry.window.height-(item_crop->y+item_crop->height)/sy,
        item_crop->width/sx,item_crop->height/sy};
      if(!Same(item_region,item.region))return fail("invalid region bounds");
    }
  }
  return true;
}
struct ReferenceStore::Impl {
  struct DerivedCrop {
    std::string id;
    std::uint64_t generation=0;
    ReadyAssetState state=ReadyAssetState::kPending;
    std::shared_ptr<const Bytes> bytes;
    std::string code="deriving";
    std::string byte_sha256;
    std::string cache_key,metadata_binding;
    std::shared_ptr<const Reference> metadata;
  };
  struct DerivedAnnotated {
    std::string id;
    std::optional<std::size_t> region_index;
    std::uint64_t generation=0;
    ReadyAssetState state=ReadyAssetState::kPending;
    std::shared_ptr<const Bytes> bytes;
    std::string code="deriving";
    std::string byte_sha256;
    std::uint64_t last_access=0;
    AnnotatedRepresentation representation=AnnotatedRepresentation::kNumbered;
    std::string cache_key,metadata_binding;
  };
  struct AnnotatedWaiter {
    std::string id,cache_key,metadata_binding;
    std::optional<std::size_t> region_index;
    AnnotatedRepresentation representation;
    std::shared_ptr<const Reference> metadata;
  };
  std::shared_ptr<const ReferencePresentationState> presentation{
      std::make_shared<const ReferencePresentationState>()};
  std::shared_ptr<const std::function<Stamp()>> presentation_now;
  mutable std::mutex mutex;
  std::condition_variable work_ready;
  std::condition_variable idle;
  std::deque<std::function<void()>> work;
  std::unordered_map<std::string,ReferenceJobSnapshot> jobs;
  std::unordered_map<std::string,std::shared_ptr<std::atomic_bool>> append_operations;
  std::unordered_map<std::string,std::size_t> reservations;
  std::unordered_map<std::string,std::size_t> owners;
  std::unordered_map<std::string,std::uint64_t> ready_access;
  std::unordered_map<std::string,ReferenceStore::Completion> completions;
  std::optional<DerivedCrop> derived_crop;
  std::optional<DerivedAnnotated> derived_annotated;
  std::unordered_map<std::string,DerivedAnnotated> annotated_cache;
  // Bounded request identities only; no queued image buffers or extra executor.
  std::deque<AnnotatedWaiter> annotated_waiters;
  std::vector<std::thread> workers;
  std::thread timer;
  std::mutex retention_io_mutex;
  ReferenceStoreTestHooks hooks;
  std::size_t active=0,unfinished=0,reserved=0;
  std::size_t maximum_visible=500;
  std::int64_t retention_ttl_us=7LL*24*60*60*1'000'000;
  std::uint64_t access_clock=0;
  std::uint64_t derived_crop_generation=0;
  std::uint64_t derived_annotated_generation=0;
  bool stopping=false;
};

namespace {
constexpr std::size_t kPerJobReservation=128ULL*1024*1024;
constexpr std::int64_t kJobTimeoutUs=30'000'000;
std::int64_t FileUtcUs(const std::filesystem::path& path) {
  try {
    const auto file_time=std::filesystem::last_write_time(path);
    const auto system_time=std::chrono::time_point_cast<std::chrono::microseconds>(
        file_time-std::filesystem::file_time_type::clock::now()+
        std::chrono::system_clock::now());
    return system_time.time_since_epoch().count();
  } catch(...) {return Now().utc_us;}
}
std::string TerminalStateText(std::string_view code,std::int64_t accepted,
                              std::int64_t terminal) {
  return std::string(code)+"\n"+std::to_string(accepted)+"\n"+
         std::to_string(terminal)+"\n";
}
// Preserve the lifecycle text prefix for old readers. New readers retain the
// exact frozen context even if a pending capture is interrupted before pixels.
std::string LifecycleStateText(std::string_view code,std::int64_t accepted,
    std::int64_t terminal,const std::shared_ptr<const Reference>& metadata) {
  auto result=TerminalStateText(code,accepted,terminal);
  if(metadata) {
    result+="seethis-context-v1\n";
    const auto bytes=Serialize(*metadata);result.append(bytes.begin(),bytes.end());
  }
  return result;
}
void ReadLifecycleContext(const std::filesystem::path& path,
                           ReferenceJobSnapshot& job) {
  if(std::filesystem::file_size(path)>4*1024*1024)return;
  const auto bytes=ReadFile(path);
  const std::string text(bytes.begin(),bytes.end());
  std::istringstream input(text);std::string code,accepted,terminal,magic;
  if(!std::getline(input,code)||!std::getline(input,accepted)||
     !std::getline(input,terminal)||!std::getline(input,magic)||
     magic!="seethis-context-v1")return;
  const auto offset=input.tellg();if(offset<0)return;
  Bytes record(bytes.begin()+static_cast<std::size_t>(offset),bytes.end());
  auto reference=Deserialize(record);
  if(!reference||reference->id!=job.id||reference->mark_id!=job.id)return;
  try {
    std::size_t consumed=0;const auto value=std::stoll(accepted,&consumed);
    if(consumed!=accepted.size()||value<=0)return;job.accepted_utc_us=value;
  }catch(...) {return;}
  job.context=reference->context;job.page_identity=reference->page_identity;
  job.captured_page=reference->captured_page;job.circle_completed=reference->circle_completed;
  job.path=reference->path;job.regions=reference->regions;job.crop_margin=reference->crop_margin;
  job.metadata=std::make_shared<const Reference>(std::move(*reference));
}
std::string ReadyStateText(const ReferenceJobSnapshot& source) {
  Writer writer;auto job=source;
  Reference metadata=job.metadata?*job.metadata:job.ready->value;
  if(job.context.bundle_id=="com.google.Chrome")job.context.window_title.clear();
  if(metadata.context.bundle_id=="com.google.Chrome")
    metadata.context.window_title.clear();
  const std::uint64_t version=(metadata.context.scope.version==2||job.context.scope.version==2)?11:(metadata.context.scope.policy_present || job.context.scope.policy_present)?10:(metadata.context.title.policy_present || job.context.title.policy_present ||
      metadata.context.title.version || job.context.title.version ||
      metadata.context.title.availability!=TitleAvailability::kUnavailable ||
      job.context.title.availability!=TitleAvailability::kUnavailable)?9:
      (metadata.context.native_window.version?8:
      (metadata.captured_page?7:(metadata.page_identity?6:5)));
  metadata.source.bytes.clear();metadata.crop.bytes.clear();
  writer(version,
         job.accepted_utc_us,job.terminal_utc_us,
         job.key_down_monotonic_ms,job.key_up_monotonic_ms,
         job.circle_completed,job.crop_margin,job.context,job.path,job.regions,
         metadata);
  if (version >= 5) {
    const std::uint64_t job_subpath_version = metadata.subpath_version;
    writer(job_subpath_version);
    if (job_subpath_version >= 1)
      for (auto& region : job.regions) writer(region.subpaths);
  }
  if (metadata.schema >= 2) {
    const std::uint64_t subpath_version = metadata.subpath_version;
    writer(subpath_version);
    if (subpath_version >= 1)
      for (auto& region : metadata.regions) writer(region.subpaths);
  }
  writer(metadata.source.encoding,metadata.crop.encoding,
         metadata.crop.pixel_hash_contract,metadata.crop.pixel_sha256);
  if (version >= 6) {
    const std::uint64_t has_page_identity=metadata.page_identity?1:0;writer(has_page_identity);
    if(has_page_identity)writer(*metadata.page_identity);
  }
  if(version>=7)WriteCapturedPage(writer,metadata.captured_page);
  if(version>=8) {
    for(auto* context:{&job.context,&metadata.context}) {
      auto& identity=context->native_window;
      writer(identity.version,identity.process_start_identity_us,
             identity.observation_session,identity.incarnation);
    }
  }
  if(version>=9)for(auto* context:{&job.context,&metadata.context})
    writer(context->title.version,context->title.availability);
  if(version>=10)for(auto* context:{&job.context,&metadata.context}) {
    const auto& scope=context->scope;
    writer(scope.version,scope.kind,scope.display_id,scope.process_start_identity_us,scope.title_required);
    if(version>=11)writer(scope.process_source);
  }
  return {writer.bytes.begin(),writer.bytes.end()};
}
// Compare every positional capture field, not just the native extension. The
// original bytes stay on disk; a disagreement only retires live presentation.
bool SameCaptureContext(Context a,Context b) {
  Writer first,second;first(a);second(b);
  return first.bytes==second.bytes && a.scope==b.scope &&
      a.native_window.version==b.native_window.version &&
      a.native_window.process_start_identity_us==b.native_window.process_start_identity_us &&
      a.native_window.observation_session==b.native_window.observation_session &&
      a.native_window.incarnation==b.native_window.incarnation &&
      a.title.version==b.title.version && a.title.availability==b.title.availability;
}
bool CaptureProvenanceSupported(const Reference& r) {
  return CaptureContextProvenanceSupported(r.context) &&
      ValidStamp(r.requested) && ValidStamp(r.image_completed) && ValidStamp(r.circle_completed) &&
      r.requested.monotonic_us>=r.context.observed.monotonic_us &&
      r.image_completed.monotonic_us>=r.requested.monotonic_us &&
      r.circle_completed.monotonic_us>=r.context.observed.monotonic_us &&
      r.clock=="UTC:system_clock/us;monotonic:steady_clock/us;process-session" &&
      (r.capture_api=="ScreenCaptureKit.SCScreenshotManager.exact-window" ||
       r.capture_api=="ScreenCaptureKit.SCScreenshotManager.selected-display") &&
      (r.provenance=="historical;context-before-overlay;pixels-at-async-completion;no-semantic-object-id" ||
       r.provenance=="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id");
}
bool ValidReadyMetadata(const Reference& value,std::string_view id) {
  const auto hash=[](const std::string& value) {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](char digit) {
      return (digit>='0'&&digit<='9')||(digit>='a'&&digit<='f');
    });
  };
  const bool source_contract=value.source.encoding=="png-zlib-stored-v1"||
      value.source.encoding=="png-zlib-v1";
  const bool legacy_crop=(value.crop.encoding=="png-zlib-stored-v1"||
      value.crop.encoding=="png-zlib-v1")&&hash(value.crop.sha256)&&
      value.crop.pixel_hash_contract.empty()&&value.crop.pixel_sha256.empty();
  const bool demand_crop=value.schema==2&&value.crop.encoding=="png-zlib-v1"&&
      value.crop.sha256.empty()&&value.crop.pixel_hash_contract=="rgba8-sha256-v1"&&
      hash(value.crop.pixel_sha256);
  const auto bounded_image=[](const Image& image) {
    return image.width>0&&image.height>0&&image.width<=16384&&
        image.height<=16384&&std::uint64_t(image.width)*image.height*4<=
            128ULL*1024*1024;
  };
  const bool subpaths_ok = value.schema == 1 ? value.subpath_version == 0 :
      (value.subpath_version == 0 || value.subpath_version == 1) &&
      (value.subpath_version == 0 || std::all_of(value.regions.begin(), value.regions.end(),
          [](const auto& region) {
            return region.path.empty() && !region.subpaths.empty() &&
              std::all_of(region.subpaths.begin(), region.subpaths.end(),
                          [](const auto& path) { return path.size() >= 2; });
          }));
  const bool valid = ValidCaptureScope(value.context)&&
      (!value.context.scope.policy_present||CaptureProvenanceSupported(value))&&
      (!value.context.scope.policy_present || ((value.context.scope.kind==CaptureScopeKind::kPage)==bool(value.page_identity)))&&
      (value.schema==1||value.schema==2)&&subpaths_ok&&value.id==id&&value.mark_id==id&&SafeID(value.id)&&
      (!value.page_identity||(ValidPageIdentity(*value.page_identity)&&
       PageIdentityMatchesContext(*value.page_identity,value.context)))&&
      value.context.pid>0&&!EffectiveRegions(value).empty()&&ValidStamp(value.requested)&&
      ValidStamp(value.image_completed)&&ValidStamp(value.circle_completed)&&
      value.source.bytes.empty()&&value.crop.bytes.empty()&&
      bounded_image(value.source)&&bounded_image(value.crop)&&
      value.source.format=="image/png"&&
      value.crop.format=="image/png"&&value.source.color_space=="sRGB"&&
      value.crop.color_space=="sRGB"&&hash(value.source.sha256)&&
      source_contract&&(legacy_crop||demand_crop);
  return valid;
}
std::filesystem::path AssetPath(const std::filesystem::path& directory,
                                std::string_view id,bool crop) {
  return directory/(std::string(id)+(crop?".crop.png":".source.png"));
}
std::filesystem::path AnnotatedAssetPath(const std::filesystem::path& directory,
                                         std::string_view id) {
  return directory/(std::string(id)+".annotated.png");
}
std::filesystem::path AnnotatedDigestPath(const std::filesystem::path& directory,
                                          std::string_view id) {
  return directory/(std::string(id)+".annotated.sha256");
}
std::string AnnotatedDigestText(const Reference& metadata,
                                std::string_view digest) {
  return "seethis-annotated-png-v3-callouts\n"+metadata.id+"\n"+
      metadata.source.sha256+"\n"+std::string(digest)+"\n";
}
bool ReadReadyState(const std::filesystem::path& path,
                    ReferenceJobSnapshot* job) {
  try {
    if(std::filesystem::file_size(path)>4*1024*1024)return false;
    const auto bytes=ReadFile(path);Reader reader{bytes};std::uint64_t version=0;
    Reference metadata;reader(version);
    if(version!=2&&version!=3&&version!=4&&version!=5&&version!=6&&version!=7&&version!=8&&version!=9&&version!=10&&version!=11)return false;
    reader(job->accepted_utc_us,job->terminal_utc_us,
           job->key_down_monotonic_ms,job->key_up_monotonic_ms,
           job->circle_completed,job->crop_margin,job->context,job->path);
    if(version>=3)reader(job->regions);
    reader(metadata);
    if(version>=5) {
      std::uint64_t job_subpath_version = 0;
      reader(job_subpath_version);
      if (job_subpath_version > 1) return false;
      if (job_subpath_version == 1)
        for (auto& region : job->regions) reader(region.subpaths);
      if (metadata.schema >= 2) {
        std::uint64_t subpath_version = 0;
        reader(subpath_version);
        if (subpath_version > 1) return false;
        metadata.subpath_version = subpath_version;
        if (subpath_version == 1)
          for (auto& region : metadata.regions) reader(region.subpaths);
      }
    }
    if(version>=4)reader(metadata.source.encoding,metadata.crop.encoding,
                         metadata.crop.pixel_hash_contract,
                         metadata.crop.pixel_sha256);
    else {
      metadata.source.encoding="png-zlib-stored-v1";
      metadata.crop.encoding="png-zlib-stored-v1";
      metadata.source.pixel_hash_contract.clear();
      metadata.source.pixel_sha256.clear();
      metadata.crop.pixel_hash_contract.clear();
      metadata.crop.pixel_sha256.clear();
    }
    if (version >= 6) {
      std::uint64_t has_page_identity=0;reader(has_page_identity);
      if(has_page_identity>1||(version==6&&!has_page_identity))return false;
      if(has_page_identity) {
        PageIdentity identity;reader(identity);metadata.page_identity=std::move(identity);
      }
      job->page_identity=metadata.page_identity;
    }
    if(version>=7&&(!ReadCapturedPage(reader,metadata.captured_page)||
       (metadata.captured_page&&!CapturedPageMatches(*metadata.captured_page,metadata.page_identity,metadata.context))))return false;
    if(version>=8) {
      for(auto* context:{&job->context,&metadata.context}) {
        auto& identity=context->native_window;
        reader(identity.version,identity.process_start_identity_us,
               identity.observation_session,identity.incarnation);
        if(identity.observation_session.size()>128)return false;
      }
    }
    if(version>=9)for(auto* context:{&job->context,&metadata.context}) {
      reader(context->title.version,context->title.availability);
      context->title.policy_present=true;
    }
    if(version>=10)for(auto* context:{&job->context,&metadata.context}) {
      auto& scope=context->scope;
      reader(scope.version,scope.kind,scope.display_id,scope.process_start_identity_us,scope.title_required);
      if(version>=11)reader(scope.process_source);
      scope.policy_present=true;
    }
    if(!ValidReadyMetadata(metadata,job->id)||reader.offset!=bytes.size()||
       job->accepted_utc_us<=0||job->terminal_utc_us<=0)return false;
    const bool native_policy_supported=version!=8 ||
        (job->context.native_window.version==1 && metadata.context.native_window.version==1);
    const bool capture_consistent=native_policy_supported && SameCaptureContext(job->context,metadata.context) &&
        job->circle_completed.utc_us==metadata.circle_completed.utc_us &&
        job->circle_completed.monotonic_us==metadata.circle_completed.monotonic_us &&
        job->crop_margin==metadata.crop_margin;
    if(version>=10 && (!capture_consistent || !ValidCaptureScope(metadata.context)))return false;
    if(!capture_consistent || (metadata.context.bundle_id!="com.google.Chrome" &&
        !CaptureProvenanceSupported(metadata))) {
      for(auto* context:{&job->context,&metadata.context})
        context->title={1,TitleAvailability::kInconsistent};
    }
    job->captured_page=metadata.captured_page;
    job->metadata=std::make_shared<const Reference>(std::move(metadata));
    return true;
  } catch(...) {return false;}
}
void TrimReadyPayloads(auto& impl) {
  const auto limit=std::min(impl.maximum_visible,
                            ReferenceStore::kMaximumCachedReadyPayloads);
  while(true) {
    std::size_t retained=0;std::string oldest;std::uint64_t oldest_access=0;
    for(auto& [id,job]:impl.jobs)if(job.state==ReferenceJobState::kReady&&job.ready) {
      ++retained;
      if(auto owner=impl.owners.find(id);owner!=impl.owners.end()&&owner->second>0)
        continue;
      const auto access=impl.ready_access[id];
      if(oldest.empty()||access<oldest_access){oldest=id;oldest_access=access;}
    }
    if(retained<=limit||oldest.empty())return;
    impl.jobs[oldest].ready.reset();impl.ready_access.erase(oldest);
  }
}
// Keep completed immutable images separate from the single asynchronous work
// slot. Metadata reads for another reference must not evict a completed image
// on every poll. Both count and retained PNG bytes remain bounded.
void TrimAnnotatedCache(auto& impl) {
  for(auto entry=impl.annotated_cache.begin();entry!=impl.annotated_cache.end();) {
    const auto job=impl.jobs.find(entry->second.id);
    if(job==impl.jobs.end()||job->second.state!=ReferenceJobState::kReady)
      entry=impl.annotated_cache.erase(entry);
    else ++entry;
  }
  // A single retained reference can own several marks. Retention still prunes
  // their entries by reference ID above, so do not shrink this cache to zero
  // when maximum_visible is one.
  const auto limit=ReferenceStore::kMaximumCachedReadyPayloads-1;
  while(!impl.annotated_cache.empty()) {
    std::size_t bytes=0;
    auto oldest=impl.annotated_cache.begin();
    for(auto entry=impl.annotated_cache.begin();entry!=impl.annotated_cache.end();++entry) {
      if(entry->second.bytes)bytes+=entry->second.bytes->size();
      if(entry->second.last_access<oldest->second.last_access)oldest=entry;
    }
    if(impl.annotated_cache.size()<=limit&&bytes<=kMaxBytes)return;
    impl.annotated_cache.erase(oldest);
  }
}
void EraseAnnotatedCacheForId(auto& impl,std::string_view id) {
  for(auto entry=impl.annotated_cache.begin();entry!=impl.annotated_cache.end();) {
    if(entry->second.id==id)entry=impl.annotated_cache.erase(entry);
    else ++entry;
  }
}
Image PreparedImage(const Pixels& pixels,bool sdk_codec) {
  Image image;image.width=pixels.width;image.height=pixels.height;
  image.encoding=sdk_codec?"png-zlib-v1":"png-zlib-stored-v1";
  image.bytes=sdk_codec?EncodeSdkPng(pixels):EncodeLegacyPng(pixels);
  image.sha256=Sha256(image.bytes);return image;
}
bool PersistSmall(const std::filesystem::path& target,std::string_view value) {
  int fd=-1;std::filesystem::path temporary;
  try {
    std::filesystem::create_directories(target.parent_path());
    std::string pattern=(target.parent_path()/".job-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    fd=mkstemp(name.data());temporary=name.data();if(fd<0)return false;
    std::size_t offset=0;while(offset<value.size()) {
      const auto count=write(fd,value.data()+offset,value.size()-offset);
      if(count<=0)throw std::runtime_error("job state write failed");
      offset+=static_cast<std::size_t>(count);
    }
    if(fsync(fd)!=0)throw std::runtime_error("job state fsync failed");
    close(fd);fd=-1;
    if(rename(temporary.c_str(),target.c_str())!=0)
      throw std::runtime_error("job state publish failed");
    const int directory=open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY);
    if(directory<0)return false;
    const bool synced=fsync(directory)==0;
    close(directory);
    return synced;
  } catch(...) {
    if(fd>=0)close(fd);
    if(!temporary.empty())unlink(temporary.c_str());
    return false;
  }
}
bool PersistAsset(const std::filesystem::path& target,const Bytes& bytes) {
  return PersistSmall(target,std::string_view(
      reinterpret_cast<const char*>(bytes.data()),bytes.size()));
}
constexpr std::string_view kNotesMagic="seethis-mark-notes\n";
constexpr std::size_t kMaxNoteBytes=4096;
struct NotesSidecar {
  MarkNotesStatus status=MarkNotesStatus::kReady;
  std::uint64_t revision=0;
  std::vector<std::string> notes;
  std::vector<bool> visible;
};
bool ValidNote(std::string_view note) {
  if(note.size()>kMaxNoteBytes)return false;
  for(std::size_t i=0;i<note.size();) {
    const auto byte=static_cast<unsigned char>(note[i]);
    if(byte<0x80) {
      if((byte<0x20&&byte!='\n'&&byte!='\t')||byte==0x7f)return false;
      ++i;continue;
    }
    const int length=byte>=0xc2&&byte<=0xdf?2:
        byte>=0xe0&&byte<=0xef?3:byte>=0xf0&&byte<=0xf4?4:0;
    if(!length||i+length>note.size())return false;
    std::uint32_t code=byte&((1U<<(7-length))-1);
    for(int j=1;j<length;++j) {
      const auto tail=static_cast<unsigned char>(note[i+j]);
      if((tail&0xc0)!=0x80)return false;
      code=(code<<6)|(tail&0x3f);
    }
    if((length==2&&code<0x80)||(length==3&&code<0x800)||
       (length==4&&code<0x10000)||code>0x10ffff||
       (code>=0xd800&&code<=0xdfff))return false;
    i+=length;
  }
  return true;
}
void PutU32(std::string& out,std::uint32_t value) {
  for(int index=0;index<4;++index)out.push_back(char(value>>(index*8)));
}
void PutU64(std::string& out,std::uint64_t value) {
  for(int index=0;index<8;++index)out.push_back(char(value>>(index*8)));
}
bool GetU32(std::string_view data,std::size_t& offset,std::uint32_t& value) {
  if(data.size()-offset<4)return false;
  value=0;for(int index=0;index<4;++index)
    value|=std::uint32_t(static_cast<unsigned char>(data[offset++]))<<(index*8);
  return true;
}
bool GetU64(std::string_view data,std::size_t& offset,std::uint64_t& value) {
  if(data.size()-offset<8)return false;
  value=0;for(int index=0;index<8;++index)
    value|=std::uint64_t(static_cast<unsigned char>(data[offset++]))<<(index*8);
  return true;
}
std::filesystem::path NotesPath(const std::filesystem::path& directory,
                                std::string_view id) {
  return directory/(std::string(id)+".notes");
}
NotesSidecar ReadNotes(const std::filesystem::path& directory,
                       const Reference& reference,std::size_t count) {
  NotesSidecar result;result.notes.resize(count);result.visible.resize(count,true);
  const auto path=NotesPath(directory,reference.id);
  std::error_code ec;
  if(!std::filesystem::exists(path,ec)) {
    if(ec)result.status=MarkNotesStatus::kIOError;
    return result;
  }
  const auto size=std::filesystem::file_size(path,ec);
  if(ec){result.status=MarkNotesStatus::kIOError;return result;}
  if(size>1024*1024){result.status=MarkNotesStatus::kCorrupt;return result;}
  Bytes file;
  try {file=ReadFile(path);} catch(...) {
    result.status=MarkNotesStatus::kIOError;return result;
  }
  const std::string_view data(reinterpret_cast<const char*>(file.data()),file.size());
  if(!data.starts_with(kNotesMagic)) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  std::size_t offset=kNotesMagic.size();
  std::uint32_t version=0,stored_count=0;
  if(!GetU32(data,offset,version)) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  if(version!=1&&version!=2) {
    result.status=MarkNotesStatus::kUnsupported;return result;
  }
  if(data.size()-offset<reference.id.size()+reference.source.sha256.size()||
     data.substr(offset,reference.id.size())!=reference.id) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  offset+=reference.id.size();
  if(data.substr(offset,reference.source.sha256.size())!=reference.source.sha256) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  offset+=reference.source.sha256.size();
  if(!GetU64(data,offset,result.revision)||
     !GetU32(data,offset,stored_count)||stored_count!=count||
     result.revision==0||stored_count>1024) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  for(std::size_t slot=0;slot<count;++slot) {
    std::uint32_t note_size=0;
    if(!GetU32(data,offset,note_size)||note_size>kMaxNoteBytes||
       data.size()-offset<note_size) {
      result.status=MarkNotesStatus::kCorrupt;return result;
    }
    result.notes[slot]=std::string(data.substr(offset,note_size));
    if(!ValidNote(result.notes[slot])) {
      result.status=MarkNotesStatus::kCorrupt;return result;
    }
    offset+=note_size;
    if(version==2) {
      const auto mark_id=StableMarkId(reference.id,slot);
      if(data.size()-offset<mark_id.size()+1 ||
         data.substr(offset,mark_id.size())!=mark_id) {
        result.status=MarkNotesStatus::kCorrupt;return result;
      }
      offset+=mark_id.size();
      const auto flag=static_cast<unsigned char>(data[offset++]);
      if(flag>1){result.status=MarkNotesStatus::kCorrupt;return result;}
      result.visible[slot]=flag==1;
    }
  }
  if(offset!=data.size())result.status=MarkNotesStatus::kCorrupt;
  return result;
}
std::string EncodeNotes(const Reference& reference,const NotesSidecar& sidecar) {
  std::string bytes(kNotesMagic);
  PutU32(bytes,2);bytes+=reference.id;bytes+=reference.source.sha256;
  PutU64(bytes,sidecar.revision);
  PutU32(bytes,static_cast<std::uint32_t>(sidecar.notes.size()));
  for(std::size_t slot=0;slot<sidecar.notes.size();++slot) {
    const auto& note=sidecar.notes[slot];
    PutU32(bytes,static_cast<std::uint32_t>(note.size()));bytes+=note;
    bytes+=StableMarkId(reference.id,slot);
    bytes.push_back(sidecar.visible[slot]?1:0);
  }
  return bytes;
}
enum class NotesWriteResult { kSaved, kIOError, kCommitUncertain };
NotesWriteResult PersistNotes(const std::filesystem::path& target,
                              std::string_view data,bool fail_after_rename) {
  int fd=-1;std::filesystem::path temporary;bool renamed=false;
  try {
    std::string pattern=(target.parent_path()/".notes-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    fd=mkstemp(name.data());if(fd<0)return NotesWriteResult::kIOError;
    temporary=name.data();
    for(std::size_t offset=0;offset<data.size();) {
      const auto count=write(fd,data.data()+offset,data.size()-offset);
      if(count<=0)throw std::runtime_error("note sidecar write failed");
      offset+=static_cast<std::size_t>(count);
    }
    if(fsync(fd)!=0)throw std::runtime_error("note sidecar sync failed");
    if(close(fd)!=0)throw std::runtime_error("note sidecar close failed");
    fd=-1;
    if(rename(temporary.c_str(),target.c_str())!=0)
      throw std::runtime_error("note sidecar publish failed");
    renamed=true;
    if(fail_after_rename)return NotesWriteResult::kCommitUncertain;
    const int directory=open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY);
    if(directory<0)return NotesWriteResult::kCommitUncertain;
    const bool synced=fsync(directory)==0;close(directory);
    return synced?NotesWriteResult::kSaved:NotesWriteResult::kCommitUncertain;
  } catch(...) {
    if(fd>=0)close(fd);
    if(!renamed&&!temporary.empty())unlink(temporary.c_str());
    return renamed?NotesWriteResult::kCommitUncertain:NotesWriteResult::kIOError;
  }
}
// A manifest is the sole publication point for collection membership and notes.
// The first shot continues to refer to the original root payload, byte for byte.
std::filesystem::path CollectionPath(const std::filesystem::path& dir, std::string_view id) {
  return dir/(std::string(id)+".collection");
}
std::filesystem::path ShotPath(const std::filesystem::path& dir, std::string_view root,
                              std::string_view shot) {
  return dir/(std::string(root)+".shot-"+std::string(shot)+".png");
}
std::string EncodeCollection(const Reference& root, const ReferenceCollection& source) {
  Writer w;
  std::string magic="seethis-collection-v1",id=root.id,binding=root.source.sha256;
  auto revision=source.revision,notes_revision=source.notes_revision;
  auto count=source.shots.size();w(magic,id,binding,revision,notes_revision,count);
  for(const auto& shot:source.shots) {
    auto shot_id=shot.id;auto first=shot.first_mark_slot;
    auto metadata=Serialize(*shot.metadata);
    auto source_encoding=shot.metadata->source.encoding,crop_encoding=shot.metadata->crop.encoding;
    auto pixel_contract=shot.metadata->crop.pixel_hash_contract,pixel_hash=shot.metadata->crop.pixel_sha256;
    w(shot_id,first,metadata,source_encoding,crop_encoding,pixel_contract,pixel_hash);
  }
  auto notes=source.notes;w(notes);
  for(bool flag:source.visible){std::uint64_t value=flag?1:0;w(value);}
  return {w.bytes.begin(),w.bytes.end()};
}
ReferenceCollection ReadCollection(const std::filesystem::path& dir,const Reference& root) {
  ReferenceCollection result;result.status=MarkNotesStatus::kReady;
  try {
    const auto path=CollectionPath(dir,root.id);
    if(!std::filesystem::exists(path)) {
      auto metadata=root;metadata.source.bytes.clear();metadata.crop.bytes.clear();
      result.shots.push_back({root.id,0,std::make_shared<const Reference>(std::move(metadata))});
      const auto notes=ReadNotes(dir,root,EffectiveRegions(root).size());
      result.status=notes.status;result.notes_revision=notes.revision;
      result.notes=notes.notes;result.visible=notes.visible;return result;
    }
    if(std::filesystem::file_size(path)>8*1024*1024)throw std::runtime_error("manifest too large");
    const auto bytes=ReadFile(path);Reader r{bytes};
    std::string magic,id,binding;std::uint64_t count=0;
    r(magic,id,binding,result.revision,result.notes_revision,count);
    if(magic!="seethis-collection-v1"||id!=root.id||binding!=root.source.sha256||
       count<2||count>64||result.revision!=count||!result.notes_revision)
      throw std::runtime_error("invalid collection");
    std::size_t total=0;
    for(std::uint64_t index=0;index<count;++index) {
      std::string shot_id;std::size_t first=0;Bytes record;r(shot_id,first,record);
      std::string source_encoding,crop_encoding,pixel_contract,pixel_hash;
      r(source_encoding,crop_encoding,pixel_contract,pixel_hash);
      auto metadata=Deserialize(record);
      if(metadata) {
        metadata->source.encoding=source_encoding;metadata->crop.encoding=crop_encoding;
        metadata->crop.pixel_hash_contract=pixel_contract;metadata->crop.pixel_sha256=pixel_hash;
      }
      if((source_encoding!="png-zlib-v1"&&source_encoding!="png-zlib-stored-v1")||
         !SafeID(shot_id)||!metadata||metadata->id!=root.id||first!=total||
         !metadata->source.bytes.empty()||!metadata->crop.bytes.empty()||
         metadata->source.sha256.size()!=64||metadata->source.width==0||metadata->source.height==0||
         (index==0&&(shot_id!=root.id||metadata->source.sha256!=root.source.sha256))||
         std::any_of(result.shots.begin(),result.shots.end(),[&](const auto& old){return old.id==shot_id;}))
        throw std::runtime_error("invalid shot");
      total+=EffectiveRegions(*metadata).size();if(total>1024)throw std::runtime_error("too many marks");
      metadata->first_mark_slot=first;
      result.shots.push_back({shot_id,first,std::make_shared<const Reference>(std::move(*metadata))});
    }
    r(result.notes);if(result.notes.size()!=total)throw std::runtime_error("invalid notes");
    for(const auto& note:result.notes)if(!ValidNote(note))throw std::runtime_error("invalid note");
    for(std::size_t index=0;index<total;++index) {
      std::uint64_t flag=0;r(flag);if(flag>1)throw std::runtime_error("invalid visibility");
      result.visible.push_back(flag==1);
    }
    if(r.offset!=bytes.size())throw std::runtime_error("manifest trailing data");
  } catch(...) {result={};result.status=MarkNotesStatus::kCorrupt;}
  return result;
}
bool RemoveCollectionFiles(const std::filesystem::path& dir,std::string_view id,
    const std::function<bool(const std::filesystem::path&)>& remove={}) {
  bool success=true;
  const auto erase=[&](const std::filesystem::path& path) {
    if(remove)return remove(path);
    errno=0;return unlink(path.c_str())==0||errno==ENOENT;
  };
  const std::string prefix=std::string(id)+".shot-";
  for(const auto& item:std::filesystem::directory_iterator(dir))
    if(item.path().filename().string().starts_with(prefix))success=erase(item.path())&&success;
  return erase(CollectionPath(dir,id))&&success;
}
std::optional<DisplayPoint> FirstPoint(const ReferenceRegion& region) {
  if(!region.subpaths.empty()&&!region.subpaths.front().empty())
    return region.subpaths.front().front();
  if(!region.path.empty())return region.path.front();
  return {};
}
std::optional<Point2D> SourcePoint(const Reference& reference,
                                   const DisplayPoint& point) {
  const auto display=std::find_if(reference.context.displays.begin(),
      reference.context.displays.end(),[&](const FrozenDisplay& item) {
        return item.id==point.display_id;
      });
  if(display==reference.context.displays.end()||!point.IsValid()||
     point.unit!=CoordinateUnit::kLogicalPoints||
     point.backing_scale!=display->scale)return {};
  const double global_x=display->logical.x+point.position.x;
  const double global_y=display->logical.y+point.position.y;
  const auto& t=reference.global_to_source;
  return Point2D{t.a*global_x+t.tx,t.d*global_y+t.ty};
}
// Source labels have ten logical-pixel glyphs at ordinary 1x/2x capture.
// Export labels never grow in response to a small preview frame; display
// consumers use the separate stroke-only representation and their own labels.
int NumberPixelUnit(const Reference& reference) {
  return int(std::clamp(std::lround(2*std::min(
      std::abs(reference.global_to_source.a),
      std::abs(reference.global_to_source.d))),1L,8L));
}
struct NumberPlacement {
  Point2D anchor,endpoint;
  Rect badge;
  int unit=2;
};
bool BadgeContains(Rect rect,Point2D point,double margin=0) {
  return point.x>=rect.x-margin&&point.x<=rect.x+rect.width+margin&&
      point.y>=rect.y-margin&&point.y<=rect.y+rect.height+margin;
}
bool BadgesIntersect(Rect a,Rect b,double margin) {
  return a.x<b.x+b.width+margin&&b.x<a.x+a.width+margin&&
      a.y<b.y+b.height+margin&&b.y<a.y+a.height+margin;
}
// Closed segment/expanded rectangle intersection, including the endpoint.
bool LeaderIntersects(Point2D a,Point2D b,Rect rect,double margin) {
  double first=0,last=1;
  const auto clip=[&](double origin,double delta,double low,double high) {
    if(delta==0)return origin>=low&&origin<=high;
    double lo=(low-origin)/delta,hi=(high-origin)/delta;
    if(lo>hi)std::swap(lo,hi);
    first=std::max(first,lo);last=std::min(last,hi);return first<=last;
  };
  return clip(a.x,b.x-a.x,rect.x-margin,rect.x+rect.width+margin)&&
      clip(a.y,b.y-a.y,rect.y-margin,rect.y+rect.height+margin);
}
double LeaderDistance(Point2D point,Point2D a,Point2D b) {
  const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
  const double t=length?std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/length,0.0,1.0):0;
  return std::hypot(point.x-a.x-t*dx,point.y-a.y-t*dy);
}
std::optional<std::vector<NumberPlacement>> PlaceNumbers(
    const Reference& reference,const std::vector<ReferenceRegion>& regions) {
  std::vector<NumberPlacement> result;
  std::vector<Point2D> anchors;
  for(const auto& region:regions) {
    const auto start=FirstPoint(region);
    const auto anchor=start?SourcePoint(reference,*start):std::nullopt;
    if(!anchor||!Finite(anchor->x)||!Finite(anchor->y))return {};
    anchors.push_back(*anchor);
  }
  const int unit=NumberPixelUnit(reference);
  const double gap=2*unit,rim=std::max(1,unit/2);
  const double leader_extent=rim+std::max(0.75,unit/3.0);
  for(std::size_t slot=0;slot<regions.size();++slot) {
    const auto anchor=anchors[slot];
    const double width=std::min(double(reference.source.width),double(std::max(
        11*unit,(int(std::to_string(reference.first_mark_slot+slot+1).size())*4+3)*unit)));
    const double height=std::min(double(reference.source.height),double(11*unit));
    NumberPlacement best{anchor,anchor,{0,0,width,height},unit};
    double best_distance=std::numeric_limits<double>::infinity();
    const auto consider=[&](double cx,double cy) {
      Rect badge{std::clamp(std::round(cx-width/2),0.0,double(reference.source.width)-width),
                 std::clamp(std::round(cy-height/2),0.0,double(reference.source.height)-height),
                 width,height};
      const Point2D endpoint{std::clamp(anchor.x,badge.x,badge.x+width),
                             std::clamp(anchor.y,badge.y,badge.y+height)};
      const double distance=std::hypot(endpoint.x-anchor.x,endpoint.y-anchor.y);
      if(distance>=best_distance)return;
      for(const auto point:anchors) {
        if(BadgeContains(badge,point,gap))return;
        // A leader must not appear to originate on another drawing-start point.
        if(std::hypot(point.x-anchor.x,point.y-anchor.y)>rim&&
           LeaderDistance(point,anchor,endpoint)<leader_extent+0.5)return;
      }
      for(const auto& placed:result)
        if(BadgesIntersect(badge,placed.badge,gap)||
           LeaderIntersects(anchor,endpoint,placed.badge,leader_extent)||
           LeaderIntersects(placed.anchor,placed.endpoint,badge,leader_extent))return;
      best={anchor,endpoint,badge,unit};best_distance=distance;
    };
    // Stable nearest-first rings cover adjacent/dense anchors without depending
    // on selection, visibility, note revisions, or the requested crop slot.
    for(int ring=1;ring<=32;++ring) {
      for(int dy=-ring;dy<=ring;++dy)for(int dx=-ring;dx<=ring;++dx) {
        if(std::max(std::abs(dx),std::abs(dy))!=ring)continue;
        consider(anchor.x+dx*(width/2+gap),anchor.y+dy*(height/2+gap));
      }
      if(Finite(best_distance)&&ring*(std::min(width,height)/2+gap)>
          best_distance+std::max(width,height))break;
    }
    if(!Finite(best_distance)) {
      // Deterministic packing fallback for crowded sets; a physically smaller
      // image than one glyph still produces a valid bounded derived PNG.
      for(double y=0;y+height<=reference.source.height;y+=height+gap)
        for(double x=0;x+width<=reference.source.width;x+=width+gap)
          consider(x+width/2,y+height/2);
      if(!Finite(best_distance)) {
        best.badge.x=std::clamp(std::round(anchor.x+gap),0.0,double(reference.source.width)-width);
        best.badge.y=std::clamp(std::round(anchor.y+gap),0.0,double(reference.source.height)-height);
        best.endpoint={std::clamp(anchor.x,best.badge.x,best.badge.x+width),
                       std::clamp(anchor.y,best.badge.y,best.badge.y+height)};
      }
    }
    result.push_back(best);
  }
  return result;
}
std::optional<Rect> NumberedMarkBounds(const Reference& reference,
    std::size_t slot,const std::vector<ReferenceRegion>& regions,
    const std::vector<NumberPlacement>& placements) {
  if(slot>=regions.size()||slot>=placements.size())return {};
  const auto& crop=regions[slot].crop_pixels;
  const auto& p=placements[slot];
  const double margin=std::max(1,p.unit/2)+1;
  const double x0=std::max(0.0,std::floor(std::min({crop.x,p.badge.x,
      p.anchor.x-margin,p.endpoint.x-margin})));
  const double y0=std::max(0.0,std::floor(std::min({crop.y,p.badge.y,
      p.anchor.y-margin,p.endpoint.y-margin})));
  const double x1=std::min(double(reference.source.width),std::ceil(std::max({
      crop.x+crop.width,p.badge.x+p.badge.width,p.anchor.x+margin,p.endpoint.x+margin})));
  const double y1=std::min(double(reference.source.height),std::ceil(std::max({
      crop.y+crop.height,p.badge.y+p.badge.height,p.anchor.y+margin,p.endpoint.y+margin})));
  const Rect bounds{x0,y0,x1-x0,y1-y0};
  return ValidRect(bounds)?std::optional<Rect>(bounds):std::nullopt;
}
void SetNumberPixel(Pixels& pixels,int x,int y,std::array<std::uint8_t,3> rgb) {
  if(x<0||y<0||x>=int(pixels.width)||y>=int(pixels.height))return;
  const auto offset=(std::size_t(y)*pixels.width+x)*4;
  for(std::size_t channel=0;channel<3;++channel)pixels.rgba[offset+channel]=rgb[channel];
  pixels.rgba[offset+3]=255;
}
void DrawNumberLeader(Pixels& pixels,const NumberPlacement& p) {
  const double ink=std::max(0.75,p.unit/3.0),rim=std::max(1,p.unit/2);
  const int x0=std::max(0,int(std::floor(std::min(p.anchor.x,p.endpoint.x)-ink-rim)));
  const int y0=std::max(0,int(std::floor(std::min(p.anchor.y,p.endpoint.y)-ink-rim)));
  const int x1=std::min(int(pixels.width),int(std::ceil(std::max(p.anchor.x,p.endpoint.x)+ink+rim)));
  const int y1=std::min(int(pixels.height),int(std::ceil(std::max(p.anchor.y,p.endpoint.y)+ink+rim)));
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
    const double distance=LeaderDistance({x+0.5,y+0.5},p.anchor,p.endpoint);
    if(distance<=ink+rim)SetNumberPixel(pixels,x,y,distance<=ink?
        std::array<std::uint8_t,3>{45,45,45}:std::array<std::uint8_t,3>{255,255,255});
  }
}
void DrawNumberBadge(Pixels& pixels,std::size_t slot,const NumberPlacement& p) {
  static constexpr std::array<std::array<std::uint8_t,5>,10> digits={{
      {{7,5,5,5,7}},{{2,6,2,2,7}},{{7,1,7,4,7}},{{7,1,7,1,7}},
      {{5,5,7,1,1}},{{7,4,7,1,7}},{{7,4,7,5,7}},{{7,1,1,1,1}},
      {{7,5,7,5,7}},{{7,5,7,1,7}}
  }};
  const auto b=p.badge;const double corner=2*p.unit,rim=std::max(1,p.unit/2);
  for(int y=b.y;y<b.y+b.height;++y)for(int x=b.x;x<b.x+b.width;++x) {
    const double dx=std::max({b.x+corner-(x+0.5),0.0,x+0.5-(b.x+b.width-corner)});
    const double dy=std::max({b.y+corner-(y+0.5),0.0,y+0.5-(b.y+b.height-corner)});
    if(std::hypot(dx,dy)>corner)continue;
    const bool border=x<b.x+rim||x>=b.x+b.width-rim||y<b.y+rim||
        y>=b.y+b.height-rim||std::hypot(dx,dy)>corner-rim;
    SetNumberPixel(pixels,x,y,border?std::array<std::uint8_t,3>{255,255,255}:
        std::array<std::uint8_t,3>{226,35,63});
  }
  const std::string text=std::to_string(slot+1);
  const int width=(int(text.size())*4-1)*p.unit;
  const int x=int(b.x+(b.width-width)/2),y=int(b.y+(b.height-5*p.unit)/2);
  for(std::size_t n=0;n<text.size();++n)for(int row=0;row<5;++row)
    for(int column=0;column<3;++column)
      if(digits[text[n]-'0'][row]&(1<<(2-column)))
        for(int yy=0;yy<p.unit;++yy)for(int xx=0;xx<p.unit;++xx)
          SetNumberPixel(pixels,x+int(n)*4*p.unit+column*p.unit+xx,
                         y+row*p.unit+yy,{255,255,255});
}
std::optional<Bytes> DerivedCropBytes(const Reference& metadata,
                                     const Bytes& source_bytes) {
  if(source_bytes.size()<8||Sha256(source_bytes)!=metadata.source.sha256)return {};
  Image source=metadata.source;source.bytes=source_bytes;
  const auto& bounds=metadata.crop_pixels;
  Pixels crop;
  if(source.encoding=="png-zlib-stored-v1") {
    const auto pixels=DecodeLegacyPng(source);if(!pixels)return {};
    if(!ValidRect(bounds)||bounds.x<0||bounds.y<0||
       bounds.x+bounds.width>pixels->width||
       bounds.y+bounds.height>pixels->height)return {};
    const auto x=static_cast<std::size_t>(bounds.x),y=static_cast<std::size_t>(bounds.y);
    const auto width=static_cast<std::size_t>(bounds.width);
    const auto height=static_cast<std::size_t>(bounds.height);
    if(bounds.x!=x||bounds.y!=y||bounds.width!=width||bounds.height!=height)
      return {};
    crop.width=static_cast<std::uint32_t>(width);
    crop.height=static_cast<std::uint32_t>(height);crop.rgba.reserve(width*height*4);
    for(std::size_t row=0;row<height;++row) {
      const auto begin=pixels->rgba.begin()+((y+row)*pixels->width+x)*4;
      crop.rgba.insert(crop.rgba.end(),begin,begin+width*4);
    }
  } else {
    const auto inspected=InspectPng(source,bounds,true);if(!inspected)return {};
    crop=std::move(inspected->pixels);
    if(metadata.crop.pixel_hash_contract=="rgba8-sha256-v1"&&
       inspected->rgba_sha256!=metadata.crop.pixel_sha256)return {};
  }
  Bytes bytes;
  if(metadata.crop.pixel_hash_contract=="rgba8-sha256-v1") {
    bytes=EncodeSdkPng(crop);
    Image check=metadata.crop;check.bytes=bytes;check.sha256=Sha256(bytes);
    check.pixel_hash_contract.clear();check.pixel_sha256.clear();
    const auto inspected=InspectPng(check,{0,0,double(crop.width),double(crop.height)},false);
    if(!inspected||inspected->rgba_sha256!=metadata.crop.pixel_sha256)return {};
  } else {
    bytes=metadata.crop.encoding=="png-zlib-stored-v1"?
        EncodeLegacyPng(crop):EncodeSdkPng(crop);
    if(bytes.size()<8||Sha256(bytes)!=metadata.crop.sha256)return {};
  }
  return bytes;
}
std::optional<Bytes> AnnotatedBytes(const Reference& metadata,
                                    const Bytes& source_bytes,
                                    std::optional<std::size_t> region_index={},
                                    bool include_numbers=true) {
  if(source_bytes.size()<8||Sha256(source_bytes)!=metadata.source.sha256)return {};
  Image source=metadata.source;source.bytes=source_bytes;
  const auto inspected=InspectPng(source,{0,0,double(source.width),
                                           double(source.height)},true);
  if(!inspected||!ValidPixels(inspected->pixels))return {};
  Pixels pixels=std::move(inspected->pixels);
  const double radius=std::max(2.0,std::min(pixels.width,pixels.height)/500.0)/2;
  std::uint64_t visited=0,segments=0;
  bool valid=true;
  const auto source_point=[&](const DisplayPoint& point)->std::optional<Point2D> {
    const auto display=std::find_if(metadata.context.displays.begin(),
        metadata.context.displays.end(),[&](const FrozenDisplay& item) {
          return item.id==point.display_id;
        });
    if(display==metadata.context.displays.end()||!point.IsValid()||
       point.unit!=CoordinateUnit::kLogicalPoints||
       point.backing_scale!=display->scale)return {};
    const double global_x=display->logical.x+point.position.x;
    const double global_y=display->logical.y+point.position.y;
    const auto& t=metadata.global_to_source;
    const Point2D result{t.a*global_x+t.tx,t.d*global_y+t.ty};
    if(!Finite(result.x)||!Finite(result.y)||result.x<0||result.y<0||
       result.x>pixels.width||result.y>pixels.height)return {};
    return result;
  };
  const auto regions=EffectiveRegions(metadata);
  if(region_index&&*region_index>=regions.size())return {};
  if(regions.empty()&&!region_index)return source_bytes;
  const auto placements=include_numbers?PlaceNumbers(metadata,regions):
      std::optional<std::vector<NumberPlacement>>{};
  if(include_numbers&&!placements)return {};
  for(std::size_t region_number=0;region_number<regions.size();++region_number) {
    if(region_index&&*region_index!=region_number)continue;
    const auto& region=regions[region_number];
    ForEachSubpath(region,[&](const auto& path) {
      if(!valid||path.size()<2){valid=false;return;}
      auto previous=source_point(path.front());
      if(!previous){valid=false;return;}
      for(std::size_t index=1;index<path.size();++index) {
        const auto current=source_point(path[index]);
        if(!current||++segments>20'000){valid=false;return;}
        const auto left=static_cast<std::uint32_t>(std::clamp(
            std::floor(std::min(previous->x,current->x)-radius-1),
            0.0,double(pixels.width)));
        const auto right=static_cast<std::uint32_t>(std::clamp(
            std::ceil(std::max(previous->x,current->x)+radius+1),
            0.0,double(pixels.width)));
        const auto top=static_cast<std::uint32_t>(std::clamp(
            std::floor(std::min(previous->y,current->y)-radius-1),
            0.0,double(pixels.height)));
        const auto bottom=static_cast<std::uint32_t>(std::clamp(
            std::ceil(std::max(previous->y,current->y)+radius+1),
            0.0,double(pixels.height)));
        visited+=std::uint64_t(right-left)*(bottom-top);
        if(visited>128'000'000){valid=false;return;}
        const double dx=current->x-previous->x,dy=current->y-previous->y;
        const double length_squared=dx*dx+dy*dy;
        for(std::uint32_t y=top;y<bottom;++y)
          for(std::uint32_t x=left;x<right;++x) {
            const double center_x=x+0.5,center_y=y+0.5;
            const double along=length_squared>0?std::clamp(
                ((center_x-previous->x)*dx+(center_y-previous->y)*dy)/
                    length_squared,0.0,1.0):0.0;
            const double distance=std::hypot(center_x-(previous->x+along*dx),
                                             center_y-(previous->y+along*dy));
            const double coverage=std::clamp(radius+0.5-distance,0.0,1.0);
            if(coverage==0)continue;
            const auto offset=(std::size_t(y)*pixels.width+x)*4;
            constexpr std::array<std::uint8_t,3> ink={255,45,85};
            const double source_alpha=pixels.rgba[offset+3]/255.0;
            const double output_alpha=coverage+source_alpha*(1-coverage);
            for(std::size_t channel=0;channel<3;++channel)
              pixels.rgba[offset+channel]=static_cast<std::uint8_t>(std::lround(
                  (ink[channel]*coverage+pixels.rgba[offset+channel]*
                   source_alpha*(1-coverage))/output_alpha));
            pixels.rgba[offset+3]=static_cast<std::uint8_t>(
                std::lround(output_alpha*255));
          }
        previous=current;
      }
    });
    if(!valid)return {};
  }
  if(segments==0)return {};
  // Draw all leaders before labels so a leader cannot erase a numeral. The
  // placement set is identical for full images and independently derived crops.
  for(std::size_t slot=0;slot<regions.size();++slot)
    if(include_numbers&&(!region_index||*region_index==slot))DrawNumberLeader(pixels,(*placements)[slot]);
  for(std::size_t slot=0;slot<regions.size();++slot)
    if(include_numbers&&(!region_index||*region_index==slot))DrawNumberBadge(pixels,metadata.first_mark_slot+slot,(*placements)[slot]);
  if(region_index) {
    const auto numbered=NumberedMarkBounds(metadata,*region_index,regions,*placements);
    if(!numbered)return {};
    const auto& bounds=*numbered;
    if(!ValidRect(bounds)||bounds.x<0||bounds.y<0||
       bounds.x>pixels.width||bounds.y>pixels.height||
       bounds.width>pixels.width||bounds.height>pixels.height)return {};
    const auto x=static_cast<std::size_t>(bounds.x);
    const auto y=static_cast<std::size_t>(bounds.y);
    const auto width=static_cast<std::size_t>(bounds.width);
    const auto height=static_cast<std::size_t>(bounds.height);
    if(bounds.x!=x||bounds.y!=y||bounds.width!=width||bounds.height!=height||
       x+width>pixels.width||y+height>pixels.height)return {};
    Pixels cropped;
    cropped.width=static_cast<std::uint32_t>(width);
    cropped.height=static_cast<std::uint32_t>(height);
    cropped.rgba.reserve(width*height*4);
    for(std::size_t row=0;row<height;++row) {
      const auto begin=pixels.rgba.begin()+((y+row)*pixels.width+x)*4;
      cropped.rgba.insert(cropped.rgba.end(),begin,begin+width*4);
    }
    pixels=std::move(cropped);
  }
  auto bytes=EncodeSdkPng(pixels);
  if(bytes.size()<8||bytes.size()>kMaxBytes)return {};
  return bytes;
}
std::optional<StoredReference> FinalizeReference(Reference reference,
                                                 Pixels pixels,
                                                 std::string* error,
    const std::function<void(std::string_view,std::string_view)>& stage = {},
                                                 std::string_view id = {},bool allow_empty=false) {
  const auto notify=[&](std::string_view name) {if(stage)stage(name,id);};
  if(allow_empty&&EffectiveRegions(reference).empty()) {
    const auto display=std::find_if(reference.context.displays.begin(),reference.context.displays.end(),
        [&](const auto& value){return value.id==reference.capture_display_id;});
    if(!ValidCaptureScope(reference.context)||reference.context.pid<=0||
       (reference.context.scope.kind!=CaptureScopeKind::kApplication&&reference.context.pid!=reference.context.window_pid)||
       display==reference.context.displays.end()||display->pixels.width!=pixels.width||
       display->pixels.height!=pixels.height||!ValidPixels(pixels)||!ValidRect(display->logical)) {
      if(error)*error="empty_shot_display_unavailable";return {};
    }
    reference.schema=2;reference.source=PreparedImage(pixels,true);
    const auto& rect=display->logical;
    const double sx=pixels.width/rect.width,sy=pixels.height/rect.height;
    reference.global_to_source={sx,-sy,-rect.x*sx,(rect.y+rect.height)*sy};
    reference.crop_pixels={0,0,double(pixels.width),double(pixels.height)};
    reference.region=rect;reference.crop.width=pixels.width;reference.crop.height=pixels.height;
    reference.crop.pixel_hash_contract="rgba8-sha256-v1";
    reference.crop.pixel_sha256=*RgbaHash(pixels,reference.crop_pixels);
    return Canonical(reference);
  }
  Context geometry=reference.context;
  std::vector<DisplayPoint> all_path=reference.path;
  if(reference.schema==2&&!reference.regions.empty()&&
     !reference.regions.front().path.empty()) {
    const auto display_id=reference.regions.front().path.front().display_id;
    const auto display=std::find_if(geometry.displays.begin(),geometry.displays.end(),
      [&](const auto& value){return value.id==display_id;});
    if(display==geometry.displays.end()||display->pixels.width!=pixels.width||
       display->pixels.height!=pixels.height) {
      if(error)*error="selected_display_mismatch";return {};
    }
    geometry.window=display->logical;
    all_path.clear();
    for(const auto& region:reference.regions)
      ForEachSubpath(region, [&](const auto& path) {
        all_path.insert(all_path.end(), path.begin(), path.end());
      });
  } else if (reference.schema == 2 && !reference.regions.empty()) {
    const std::vector<DisplayPoint>* first = nullptr;
    for (const auto& region : reference.regions) {
      if (!region.subpaths.empty() && !region.subpaths.front().empty()) {
        first = &region.subpaths.front();
        break;
      }
    }
    if (!first) { if (error) *error = "invalid_geometry"; return {}; }
    const auto display_id = first->front().display_id;
    const auto display = std::find_if(geometry.displays.begin(), geometry.displays.end(),
      [&](const auto& value){ return value.id == display_id; });
    if (display == geometry.displays.end() || display->pixels.width != pixels.width ||
        display->pixels.height != pixels.height) {
      if (error) *error = "selected_display_mismatch"; return {};
    }
    geometry.window = display->logical;
    all_path.clear();
    for (const auto& region : reference.regions)
      ForEachSubpath(region, [&](const auto& path) {
        all_path.insert(all_path.end(), path.begin(), path.end());
      });
  }
  const auto bounds=CropBounds(geometry,all_path,pixels.width,
                               pixels.height,reference.crop_margin);
  if(!bounds){if(error)*error="invalid_geometry";return {};}
  notify("encode_begin");
  reference.source=PreparedImage(pixels,reference.schema==2);
  reference.crop_pixels=*bounds;
  const auto& window=geometry.window;
  const double sx=pixels.width/window.width,sy=pixels.height/window.height;
  reference.global_to_source={sx,-sy,-window.x*sx,
                              (window.y+window.height)*sy};
  reference.region={window.x+bounds->x/sx,
      window.y+window.height-(bounds->y+bounds->height)/sy,
      bounds->width/sx,bounds->height/sy};
  if(reference.schema==2)for(auto& item:reference.regions) {
    std::vector<DisplayPoint> item_path;
    ForEachSubpath(item, [&](const auto& path) {
      item_path.insert(item_path.end(), path.begin(), path.end());
    });
    const auto item_bounds=CropBounds(geometry,item_path,pixels.width,pixels.height,
                                      reference.crop_margin);
    if(!item_bounds){if(error)*error="invalid_region_geometry";return {};}
    item.crop_pixels=*item_bounds;
    item.region={window.x+item_bounds->x/sx,
      window.y+window.height-(item_bounds->y+item_bounds->height)/sy,
      item_bounds->width/sx,item_bounds->height/sy};
  }
  if (reference.schema == 2) {
    const bool explicit_subpaths = std::any_of(reference.regions.begin(),
        reference.regions.end(), [](const auto& region) {
          return !region.subpaths.empty();
        });
    reference.subpath_version = explicit_subpaths ? 1 : 0;
  }
  reference.crop.width=static_cast<std::uint32_t>(bounds->width);
  reference.crop.height=static_cast<std::uint32_t>(bounds->height);
  reference.crop.encoding="png-zlib-v1";
  if(reference.schema==2) {
    reference.crop.bytes.clear();reference.crop.sha256.clear();
    reference.crop.pixel_hash_contract="rgba8-sha256-v1";
    const auto digest=RgbaHash(pixels,*bounds);
    if(!digest){if(error)*error="invalid_crop_hash";return {};}
    reference.crop.pixel_sha256=*digest;
  } else {
    Pixels crop;crop.width=reference.crop.width;crop.height=reference.crop.height;
    crop.rgba.reserve(std::size_t(crop.width)*crop.height*4);
    for(std::size_t y=0;y<crop.height;++y) {
      const auto begin=pixels.rgba.begin()+
          ((y+static_cast<std::size_t>(bounds->y))*pixels.width+
           static_cast<std::size_t>(bounds->x))*4;
      crop.rgba.insert(crop.rgba.end(),begin,begin+crop.width*4);
    }
    reference.crop=PreparedImage(crop,false);
  }
  notify("encode_end");
  notify("validate_begin");
  if(!Validate(reference,error)){notify("validate_end");return {};}
  notify("validate_end");
  notify("serialize_begin");
  auto stored=Canonical(reference);
  notify("serialize_end");
  return stored;
}
}  // namespace

namespace {
MarkNotesSnapshot PresentationNotes(std::string_view id,const ReferenceCollection& collection) {
  MarkNotesSnapshot result;result.status=collection.status;
  if(collection.status!=MarkNotesStatus::kReady)return result;
  result.status=MarkNotesStatus::kReady;result.revision=collection.notes_revision;
  for(const auto& shot:collection.shots) {
    const auto& reference=*shot.metadata;const auto regions=EffectiveRegions(reference);
    const auto placements=PlaceNumbers(reference,regions);
    if(!placements){result.status=MarkNotesStatus::kCorrupt;result.marks.clear();return result;}
    for(std::size_t local=0;local<regions.size();++local) {
      const auto slot=shot.first_mark_slot+local;
      const auto anchor=FirstPoint(regions[local]);
      const auto bounds=NumberedMarkBounds(reference,local,regions,*placements);
      if(!anchor||!bounds){result.status=MarkNotesStatus::kCorrupt;result.marks.clear();return result;}
      result.marks.push_back({StableMarkId(id,slot),slot,static_cast<std::uint32_t>(slot+1),
          *anchor,*bounds,collection.notes[slot],collection.visible[slot],collection.notes_revision,
          shot.id,local});
    }
  }
  return result;
}
}
void ReferenceStore::PublishPresentationLocked(const ReferenceJobSnapshot& job,ReferenceCollection collection) {
  const auto previous=std::atomic_load(&impl_->presentation);
  if(const auto found=previous->by_id.find(job.id);found!=previous->by_id.end()) {
    const auto& old=*found->second;
    const bool sameCollection=old.collection.status==collection.status&&
        (collection.status!=MarkNotesStatus::kReady||
         (job.metadata&&EncodeCollection(*job.metadata,old.collection)==EncodeCollection(*job.metadata,collection)));
    if(old.job.state==job.state&&old.job.code==job.code&&old.job.metadata==job.metadata&&
       old.retained_until_utc_us==job.accepted_utc_us+impl_->retention_ttl_us&&sameCollection)return;
  }
  auto entry=std::make_shared<ReferencePresentation>();
  entry->job=job;entry->job.ready.reset();
  entry->collection=std::move(collection);
  entry->notes=PresentationNotes(job.id,entry->collection);
  if(job.state==ReferenceJobState::kPending&&job.metadata) {
    entry->notes.status=MarkNotesStatus::kPending;
    const auto regions=EffectiveRegions(*job.metadata);
    for(std::size_t slot=0;slot<regions.size();++slot)if(const auto anchor=FirstPoint(regions[slot]))
      entry->notes.marks.push_back({StableMarkId(job.id,slot),slot,static_cast<std::uint32_t>(slot+1),
          *anchor,{},"",true,0,job.id,slot});
  }
  for(std::size_t index=0;index<entry->collection.shots.size();++index) {
    const auto& shot=entry->collection.shots[index];
    if(!shot.metadata)continue;
    entry->source_membership.emplace(std::to_string(shot.first_mark_slot)+":"+shot.metadata->source.sha256,index);
    auto& notes=entry->shot_notes[shot.id];notes.status=entry->notes.status;notes.revision=entry->notes.revision;
  }
  for(const auto& mark:entry->notes.marks)entry->shot_notes[mark.shot_id].marks.push_back(mark);
  entry->retained_until_utc_us=job.accepted_utc_us+impl_->retention_ttl_us;
  auto state=std::make_shared<ReferencePresentationState>(*std::atomic_load(&impl_->presentation));
  ++state->revision;
  std::erase_if(state->jobs,[&](const auto& old){return old->job.id==job.id;});
  state->by_id.erase(job.id);
  // Discovered corrupt payloads retain their historical failure status, but
  // must not re-enter the Pending/Failed live-stroke presentation fallback.
  if(job.state!=ReferenceJobState::kDeleted&&job.state!=ReferenceJobState::kExpired&&
     !(job.state==ReferenceJobState::kFailed&&job.code=="corrupt")) {
    state->jobs.push_back(entry);state->by_id.emplace(job.id,entry);
    std::sort(state->jobs.begin(),state->jobs.end(),[](const auto& a,const auto& b) {
      return a->job.accepted_utc_us==b->job.accepted_utc_us?a->job.id<b->job.id:
          a->job.accepted_utc_us<b->job.accepted_utc_us;
    });
  }
  state->has_chrome_context=false;state->by_window.clear();state->by_application.clear();
  for(const auto& value:state->jobs) {
    std::set<std::string> windows,applications;
    const auto index=[&](const Context& context) {
      if(!ValidCaptureScope(context))return;
      if(context.scope.kind==CaptureScopeKind::kApplication)
        applications.insert(ApplicationSourceKey(context.pid,context.scope.process_start_identity_us,context.scope.display_id,CaptureProcessSource(context)));
      else if(context.window_id)windows.insert(WindowSourceKey(context.window_pid,context.window_id));
    };
    index(value->job.context);
    for(const auto& shot:value->collection.shots)if(shot.metadata)index(shot.metadata->context);
    for(const auto& key:windows)state->by_window[key].push_back(value);
    for(const auto& key:applications)state->by_application[key].push_back(value);
    if(value->job.context.bundle_id=="com.google.Chrome")state->has_chrome_context=true;
    for(const auto& shot:value->collection.shots)
      if(shot.metadata&&shot.metadata->context.bundle_id=="com.google.Chrome")state->has_chrome_context=true;
  }
  std::atomic_store(&impl_->presentation,std::shared_ptr<const ReferencePresentationState>(std::move(state)));
}
std::shared_ptr<const ReferencePresentationState> ReferenceStore::ReadPresentation() const {
  return std::atomic_load(&impl_->presentation);
}
std::shared_ptr<const ReferencePresentation> ReferenceStore::ReadPresentation(std::string_view id) const {
  const auto state=ReadPresentation();const auto found=state->by_id.find(std::string(id));
  if(found==state->by_id.end())return {};
  const auto& entry=found->second;
  // Retention remains a live deadline even between maintenance passes.
  const auto clock=std::atomic_load(&impl_->presentation_now);
  const auto now=clock?(*clock)():Now();
  if(entry->job.accepted_utc_us<=0||now.utc_us>entry->retained_until_utc_us)return {};
  return entry;
}

ReferenceCollection ReferenceStore::Collection(std::string_view id) const {
  const auto job=LookupMetadata(id);
  ReferenceCollection result;
  if(!job)return result;
  if(job->state!=ReferenceJobState::kReady||!job->metadata) {
    result.status=job->state==ReferenceJobState::kDeleted?MarkNotesStatus::kDeleted:
      job->state==ReferenceJobState::kExpired?MarkNotesStatus::kExpired:MarkNotesStatus::kFailed;
    return result;
  }
  std::lock_guard io_lock(impl_->retention_io_mutex);
  result=ReadCollection(directory_,*job->metadata);
  {std::lock_guard lock(impl_->mutex);
    const auto current=impl_->jobs.find(job->id);
    if(current!=impl_->jobs.end()&&current->second.state==ReferenceJobState::kReady&&
       current->second.metadata==job->metadata)
      const_cast<ReferenceStore*>(this)->PublishPresentationLocked(current->second,result);
  }
  return result;
}
void ReferenceStore::AppendReferenceAsync(std::string root,std::uint64_t expected,
    std::string operation,Reference metadata,CaptureResult capture,AppendCompletion completion,
    std::int64_t deadline_monotonic_us) {
  AppendAsync(std::move(root),{},expected,std::move(operation),std::move(metadata),
      std::move(capture),std::move(completion),deadline_monotonic_us);
}
void ReferenceStore::AppendAsync(std::string root_id,std::string selected_mark_id,
    std::uint64_t expected,std::string operation,Reference metadata,CaptureResult capture,
    AppendCompletion completion,std::int64_t deadline_monotonic_us) {
  const auto report=[&](AppendStatus status) {
    if(completion)completion({status,root_id,operation,0});
  };
  if(!SafeID(root_id)||!SafeID(operation)||operation==root_id||
     (!selected_mark_id.empty()&&!SafeID(selected_mark_id))||!ValidPixels(capture.pixels)||!capture.error.empty()) {
    report(AppendStatus::kInvalid);return;
  }
  Prune();
  std::function<Stamp()> clock;
  {std::lock_guard guard(impl_->mutex);clock=impl_->hooks.now;}
  const auto admitted=clock?clock():Now();
  // operation_started is immutable gesture evidence, not a service deadline.
  const auto deadline=deadline_monotonic_us>0?
      std::min(deadline_monotonic_us,admitted.monotonic_us+kJobTimeoutUs):
      admitted.monotonic_us+kJobTimeoutUs;
  std::unique_lock lock(impl_->mutex);
  if(impl_->stopping||impl_->unfinished>=kMaximumUnfinishedJobs||
     impl_->reserved+kPerJobReservation>kMaximumReservedBytes) {
    lock.unlock();report(AppendStatus::kBusy);return;
  }
  if(impl_->append_operations.contains(operation)){lock.unlock();report(AppendStatus::kBusy);return;}
  auto active=std::make_shared<std::atomic_bool>(true);impl_->append_operations[operation]=active;
  ++impl_->unfinished;impl_->reserved+=kPerJobReservation;
  impl_->work.push_back([this,root_id,selected_mark_id,expected,operation,
      metadata=std::move(metadata),capture=std::move(capture),completion=std::move(completion),deadline,active]() mutable {
    AppendResult result{AppendStatus::kInvalid,root_id,operation,0};
    const auto release=std::shared_ptr<void>(nullptr,[&](void*) {
      {std::lock_guard guard(impl_->mutex);--impl_->unfinished;impl_->reserved-=kPerJobReservation;impl_->append_operations.erase(operation);}
      if(completion)completion(result);
    });
    ReferenceStoreTestHooks hooks;
    {std::lock_guard guard(impl_->mutex);hooks=impl_->hooks;}
    metadata.id=root_id;metadata.mark_id=root_id;
    metadata.requested=capture.requested;metadata.image_completed=capture.completed;
    std::string error;
    auto stored=FinalizeReference(std::move(metadata),std::move(capture.pixels),&error,hooks.stage,root_id,true);
    if(!stored||hooks.fail_save){result.status=AppendStatus::kIOError;return;}
    if(hooks.stage)hooks.stage("append_payload",root_id);
    if(!active->load()){result.status=AppendStatus::kCancelled;return;}
    const auto payload=ShotPath(directory_,root_id,operation);
    // IO serialization covers payload through membership publication. Notes can
    // change during capture/encoding; the latest notes are read inside this lock.
    std::lock_guard io_lock(impl_->retention_io_mutex);
    std::unique_lock guard(impl_->mutex);
    const auto job=impl_->jobs.find(root_id);
    const auto now=hooks.now?hooks.now():Now();
    if(job==impl_->jobs.end()||job->second.state!=ReferenceJobState::kReady||
       !job->second.metadata||std::filesystem::exists(directory_/(root_id+".deleted"))||
       now.utc_us-job->second.accepted_utc_us>impl_->retention_ttl_us) {
      result.status=AppendStatus::kUnavailable;return;
    }
    const auto root_metadata=job->second.metadata;
    auto collection=ReadCollection(directory_,*root_metadata);
    PublishPresentationLocked(job->second,collection);
    if(collection.status!=MarkNotesStatus::kReady){result.status=AppendStatus::kInvalid;return;}
    for(const auto& shot:collection.shots)if(shot.id==operation) {
      const int directory=open(directory_.c_str(),O_RDONLY|O_DIRECTORY);
      const bool synced=directory>=0&&fsync(directory)==0;if(directory>=0)close(directory);
      result.status=synced?AppendStatus::kIdempotent:AppendStatus::kCommitUncertain;
      result.revision=collection.revision;return;
    }
    if(collection.revision!=expected){result.status=AppendStatus::kStale;return;}
    bool owner=selected_mark_id.empty(); // Explicit reference target has no mark prerequisite.
    for(std::size_t slot=0;slot<collection.notes.size();++slot)
      if(StableMarkId(root_id,slot)==selected_mark_id)owner=true;
    const auto count=EffectiveRegions(stored->value).size();
    if(!owner||collection.shots.size()>=64||collection.notes.size()+count>1024||
       now.monotonic_us>deadline) {
      result.status=AppendStatus::kInvalid;return;
    }
    guard.unlock();
    if(!PersistAsset(payload,stored->value.source.bytes)||
       Sha256File(payload)!=stored->value.source.sha256) {
      result.status=AppendStatus::kIOError;return;
    }
    auto shot=stored->value;shot.source.bytes.clear();shot.crop.bytes.clear();
    shot.first_mark_slot=collection.notes.size();
    collection.shots.push_back({operation,shot.first_mark_slot,
        std::make_shared<const Reference>(std::move(shot))});
    collection.notes.resize(collection.notes.size()+count);
    collection.visible.resize(collection.notes.size(),true);
    ++collection.revision;++collection.notes_revision;
    if(hooks.stage)hooks.stage("append_before_publish",root_id);
    if(hooks.fail_append_before_publish){result.status=AppendStatus::kIOError;return;}
    guard.lock();
    const auto current_job=impl_->jobs.find(root_id);
    if(current_job==impl_->jobs.end()||current_job->second.state!=ReferenceJobState::kReady||
       std::filesystem::exists(directory_/(root_id+".deleted"))) {
      result.status=AppendStatus::kUnavailable;return;
    }
    const auto publish_now=hooks.now?hooks.now():Now();
    if(!active->load()||publish_now.monotonic_us>deadline){result.status=AppendStatus::kCancelled;return;}
    const auto persisted=PersistNotes(CollectionPath(directory_,root_id),
        EncodeCollection(*root_metadata,collection),hooks.fail_append_after_rename);
    PublishPresentationLocked(current_job->second,ReadCollection(directory_,*root_metadata));
    result.revision=collection.revision;
    result.status=persisted==NotesWriteResult::kSaved?AppendStatus::kCommitted:
      persisted==NotesWriteResult::kCommitUncertain?AppendStatus::kCommitUncertain:AppendStatus::kIOError;
    // An uncertain rename is reconciled by the same operation ID on readback.
  });
  lock.unlock();impl_->work_ready.notify_one();
}
void ReferenceStore::CancelAppend(std::string_view operation) {
  std::lock_guard lock(impl_->mutex);
  const auto found=impl_->append_operations.find(std::string(operation));
  if(found!=impl_->append_operations.end())found->second->store(false);
}
ReadyAssetResult ReferenceStore::RequestShotAnnotated(std::string_view id,
    std::string_view shot_id,bool numbers) {
  const auto collection=Collection(id);
  if(collection.status!=MarkNotesStatus::kReady)return {ReadyAssetState::kFailed,{},"collection_unavailable",{}};
  const auto shot=std::find_if(collection.shots.begin(),collection.shots.end(),
      [&](const auto& value){return value.id==shot_id;});
  if(shot==collection.shots.end())return {ReadyAssetState::kFailed,{},"unknown_shot",{}};
  if(!collection.multi())return numbers?RequestReadyAnnotated(id):RequestReadyDisplayUnderlay(id);
  const std::string root(id),shot_identity(shot_id);
  const auto metadata=shot->metadata;
  const auto revision=collection.revision;
  const std::string key=root+":shot:"+shot_identity+":"+metadata->source.sha256+":"+
      std::to_string(shot->first_mark_slot)+":"+std::to_string(revision)+(numbers?":numbers":":strokes");
  const auto path=shot==collection.shots.begin()?AssetPath(directory_,id,false):ShotPath(directory_,id,shot_id);
  {
    std::lock_guard lock(impl_->mutex);
    if(auto entry=impl_->annotated_cache.find(key);entry!=impl_->annotated_cache.end()) {
      entry->second.last_access=++impl_->access_clock;
      return {entry->second.state,entry->second.bytes,entry->second.code,entry->second.byte_sha256};
    }
    if(impl_->stopping||impl_->annotated_cache.size()>=32)
      return {ReadyAssetState::kPending,{},"queued",{}};
    auto& pending=impl_->annotated_cache[key];pending.id=root;pending.cache_key=key;
    pending.last_access=++impl_->access_clock;
    impl_->work.push_back([this,root,shot_identity,metadata,revision,numbers,key,path] {
      std::optional<Bytes> bytes;
      try {
        if(std::filesystem::file_size(path)<=kMaxBytes)
          bytes=AnnotatedBytes(*metadata,ReadFile(path),{},numbers);
      } catch(...) {}
      const auto current=Collection(root);
      std::lock_guard lock(impl_->mutex);
      const auto entry=impl_->annotated_cache.find(key);if(entry==impl_->annotated_cache.end())return;
      if(current.status!=MarkNotesStatus::kReady||current.revision!=revision) {
        impl_->annotated_cache.erase(entry);return;
      }
      entry->second.state=bytes?ReadyAssetState::kReady:ReadyAssetState::kFailed;
      entry->second.code=bytes?"":"asset_corrupt";
      if(bytes) {
        entry->second.byte_sha256=Sha256(*bytes);
        entry->second.bytes=std::make_shared<const Bytes>(std::move(*bytes));
      }
      TrimAnnotatedCache(*impl_);
    });
  }
  impl_->work_ready.notify_one();return {ReadyAssetState::kPending,{},"deriving",{}};
}

ReferenceStore::ReferenceStore(std::filesystem::path directory)
    :directory_(std::move(directory)),impl_(std::make_unique<Impl>()) {
  std::filesystem::create_directories(directory_);
  // The tombstone manifest and small per-ID sidecars build a bounded index.
  // Full immutable records are loaded only for the requested ID.
  {
    std::ifstream tombstones(directory_/".tombstones");
    std::string id;std::int64_t accepted=0,terminal=0,expired=0;
    while(tombstones>>id>>accepted>>terminal>>expired)if(SafeID(id)) {
      ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kExpired;
      job.code="expired_reference";job.accepted_utc_us=accepted;
      job.terminal_utc_us=terminal;job.expired_utc_us=expired;
      impl_->jobs[id]=std::move(job);
    }
  }
  for(const auto& item:std::filesystem::directory_iterator(directory_)) {
    if(item.path().extension()!=".deleted")continue;
    const auto id=item.path().stem().string();if(!SafeID(id))continue;
    ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kDeleted;
    std::ifstream input(item.path());std::getline(input,job.code);
    (void)(input>>job.accepted_utc_us>>job.terminal_utc_us);
    if(job.code.empty())job.code="deleted_reference";
    if(job.accepted_utc_us<=0)job.accepted_utc_us=FileUtcUs(item.path());
    if(job.terminal_utc_us<=0)job.terminal_utc_us=job.accepted_utc_us;
    impl_->jobs[id]=std::move(job);
  }
  for(const auto& item:std::filesystem::directory_iterator(directory_)) {
    if(item.path().extension()!=".stref")continue;
    const auto id=item.path().stem().string();if(!SafeID(id))continue;
    if(auto found=impl_->jobs.find(id);found!=impl_->jobs.end()&&
       (found->second.state==ReferenceJobState::kExpired||
        found->second.state==ReferenceJobState::kDeleted))continue;
    ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kReady;
    job.accepted_utc_us=FileUtcUs(item.path());job.terminal_utc_us=job.accepted_utc_us;
    if(!ReadReadyState(directory_/(id+".ready"),&job)) {
      job.state=ReferenceJobState::kIndexing;job.code="indexing";
    }
    if(job.accepted_utc_us<=0)job.accepted_utc_us=FileUtcUs(item.path());
    if(job.terminal_utc_us<=0)job.terminal_utc_us=job.accepted_utc_us;
    impl_->jobs[id]=std::move(job);
  }
  for(const auto& item:std::filesystem::directory_iterator(directory_)) {
    if(item.path().extension()!=".ready")continue;
    const auto id=item.path().stem().string();if(!SafeID(id)||impl_->jobs.contains(id))continue;
    ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kReady;
    job.accepted_utc_us=FileUtcUs(item.path());job.terminal_utc_us=job.accepted_utc_us;
    if(!ReadReadyState(item.path(),&job)||!job.metadata||job.metadata->schema!=2||
       !std::filesystem::exists(AssetPath(directory_,id,false))) {
      job.state=ReferenceJobState::kFailed;job.code="corrupt";
    }
    impl_->jobs[id]=std::move(job);
  }
  for(const auto& item:std::filesystem::directory_iterator(directory_)) {
    const auto extension=item.path().extension();
    if(extension!=".pending"&&extension!=".failed")continue;
    const auto id=item.path().stem().string();if(!SafeID(id))continue;
    if(auto found=impl_->jobs.find(id);found!=impl_->jobs.end()&&
       (found->second.state==ReferenceJobState::kReady||
        found->second.state==ReferenceJobState::kExpired||
        found->second.state==ReferenceJobState::kDeleted))continue;
    ReferenceJobSnapshot job;job.id=id;job.state=ReferenceJobState::kFailed;
    job.code=extension==".pending"?"interrupted":"";
    job.accepted_utc_us=FileUtcUs(item.path());job.terminal_utc_us=job.accepted_utc_us;
    if(extension==".failed") {
      std::ifstream input(item.path());std::getline(input,job.code);
      (void)(input>>job.accepted_utc_us>>job.terminal_utc_us);
      if(job.code.empty())job.code="interrupted";
    }
    try {ReadLifecycleContext(item.path(),job);}catch(...) {}
    if(job.accepted_utc_us<=0)job.accepted_utc_us=FileUtcUs(item.path());
    if(job.terminal_utc_us<=0)job.terminal_utc_us=job.accepted_utc_us;
    impl_->jobs[id]=job;
    if(extension==".pending") {
      (void)PersistSmall(directory_/(id+".failed"),LifecycleStateText(
          job.code,job.accepted_utc_us,job.terminal_utc_us,job.metadata));
      unlink(item.path().c_str());
    }
  }
  std::vector<std::string> deleted_ids;
  for(const auto& [id,job]:impl_->jobs)
    if(job.state==ReferenceJobState::kDeleted)deleted_ids.push_back(id);
  for(const auto& id:deleted_ids) {
    const bool cleaned=CleanupDeletedFiles(id);
    auto& job=impl_->jobs[id];
    job.code=cleaned?"deleted_reference":"delete_cleanup_failed";
    (void)PersistSmall(directory_/(id+".deleted"),TerminalStateText(
        job.code,job.accepted_utc_us,job.terminal_utc_us));
  }
  for(const auto& [id,job]:impl_->jobs)
    PublishPresentationLocked(job,job.state==ReferenceJobState::kReady&&job.metadata?
        ReadCollection(directory_,*job.metadata):ReferenceCollection{});
  for(int index=0;index<2;++index)impl_->workers.emplace_back([this] {
    for(;;) {std::function<void()> task;{
      std::unique_lock lock(impl_->mutex);impl_->work_ready.wait(lock,[&]{return impl_->stopping||!impl_->work.empty();});
      if(impl_->work.empty()){if(impl_->stopping)return;continue;}
      task=std::move(impl_->work.front());impl_->work.pop_front();++impl_->active;}
      try{task();}catch(...){}
      {std::lock_guard lock(impl_->mutex);--impl_->active;if(impl_->work.empty()&&impl_->active==0)impl_->idle.notify_all();}
    }
  });
  impl_->timer=std::thread([this] {
    while(true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      std::function<Stamp()> clock;
      {std::lock_guard lock(impl_->mutex);if(impl_->stopping)return;
        clock=impl_->hooks.now;}
      const auto now=clock?clock().monotonic_us:Now().monotonic_us;
      std::vector<std::string> expired;
      {std::lock_guard lock(impl_->mutex);if(impl_->stopping)return;
        for(const auto& [id,job]:impl_->jobs)
          if(job.state==ReferenceJobState::kPending && job.code!="committing" &&
             job.deadline_monotonic_us>0 &&
             now>=job.deadline_monotonic_us)expired.push_back(id);}
      for(const auto& id:expired)Fail(id,"timeout");
    }
  });
  Prune();
  ScheduleLegacyMigrations();
}

ReferenceStore::~ReferenceStore() {
  std::vector<std::string> pending;{
    std::lock_guard lock(impl_->mutex);for(const auto& [id,job]:impl_->jobs)
      if(job.state==ReferenceJobState::kPending)pending.push_back(id);}
  for(const auto& id:pending)Fail(id,"shutdown");
  {std::lock_guard lock(impl_->mutex);impl_->stopping=true;}
  impl_->work_ready.notify_all();if(impl_->timer.joinable())impl_->timer.join();
  for(auto& worker:impl_->workers)if(worker.joinable())worker.join();
}
SaveResult ReferenceStore::Save(const StoredReference& ref,std::string* error) {
  std::function<void(std::string_view,std::string_view)> stage;
  {std::lock_guard lock(impl_->mutex);stage=impl_->hooks.stage;}
  const auto notify=[&](std::string_view name) {if(stage)stage(name,ref.value.id);};
  if(ref.value.schema==2) {
    if(error)*error="schema-2 references use lightweight ready metadata";
    return SaveResult::kInvalid;
  }
  if(!Validate(ref.value,error))return SaveResult::kInvalid;
  notify("canonical_check_begin");
  const auto expected=Canonical(ref.value);
  notify("canonical_check_end");
  if(expected.record!=ref.record || expected.clipboard_text!=ref.clipboard_text || expected.clipboard_png!=ref.clipboard_png)return SaveResult::kInvalid;
  notify("pack_begin");
  const auto data=Pack(ref);if(data.size()>kMaxBytes)return SaveResult::kInvalid;
  notify("pack_end");
  std::lock_guard io_lock(impl_->retention_io_mutex);
  int fd=-1,dir=-1;std::filesystem::path staging;
  try {
    std::filesystem::create_directories(directory_);
    dir=open(directory_.c_str(),O_RDONLY|O_DIRECTORY);if(dir<0)throw std::runtime_error("open store directory failed");
    if(std::filesystem::exists(directory_/(ref.value.id+".deleted"))) {
      close(dir);return SaveResult::kDeleted;
    }
    const auto target=directory_/(ref.value.id+".stref");
    if(std::filesystem::exists(target)) {
      const bool same=ReadFile(target)==data;if(fsync(dir)!=0)throw std::runtime_error("idempotent parent fsync failed");close(dir);return same?SaveResult::kIdempotent:SaveResult::kConflict;
    }
    std::string pattern=(directory_/".stage-XXXXXX").string();std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    fd=mkstemp(name.data());staging=name.data();if(fd<0)throw std::runtime_error("create staging failed");
    notify("record_write_begin");
    std::size_t pos=0;while(pos<data.size()) { auto n=write(fd,data.data()+pos,data.size()-pos);if(n<=0)throw std::runtime_error("write staging failed");pos+=n; }
    if(fsync(fd)!=0)throw std::runtime_error("staging fsync failed");close(fd);fd=-1;
    notify("record_write_end");
    notify("record_readback_begin");
    const auto readback=ReadFile(staging);
    const bool readback_valid=readback==data&&Unpack(readback).has_value();
    notify("record_readback_end");
    if(!readback_valid)throw std::runtime_error("staging hash validation failed");
    // Hard-link publication is atomic and never replaces an existing identity.
    if(link(staging.c_str(),target.c_str())!=0) {
      if(errno!=EEXIST)throw std::runtime_error("atomic publish failed");
      const bool same=ReadFile(target)==data;unlink(staging.c_str());close(dir);
      return same?SaveResult::kIdempotent:SaveResult::kConflict;
    }
    if(unlink(staging.c_str())!=0 || fsync(dir)!=0)throw std::runtime_error("published record directory fsync failed");
    close(dir);return SaveResult::kSaved;
  }catch(const std::exception& e) {
    if(fd>=0)close(fd);if(dir>=0)close(dir);if(!staging.empty())unlink(staging.c_str());if(error)*error=e.what();return SaveResult::kIOError;
  }
}
std::vector<StoredReference> ReferenceStore::Load(std::vector<std::string>* errors) const {
  std::vector<StoredReference> out;
  try {
    if(!std::filesystem::exists(directory_))return out;
    for(const auto& item:std::filesystem::directory_iterator(directory_)) {
      if(item.path().extension()!=".stref")continue;
      try {
        auto record=Unpack(ReadFile(item.path()));
        if(!record || item.path().stem()!=record->value.id)throw std::runtime_error("corrupt reference");
        out.push_back(std::move(*record));
      }catch(const std::exception& e){if(errors)errors->push_back(item.path().filename().string()+": "+e.what());}
    }
  }catch(const std::exception& e){if(errors)errors->push_back(e.what());}
  std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.value.context.observed.utc_us==b.value.context.observed.utc_us?
    a.value.id<b.value.id:a.value.context.observed.utc_us<b.value.context.observed.utc_us;});return out;
}

void ReferenceStore::SetRetentionPolicy(std::int64_t ttl_seconds,
                                        std::size_t maximum_visible) {
  {std::lock_guard lock(impl_->mutex);
    impl_->retention_ttl_us=std::clamp<std::int64_t>(ttl_seconds,1,
        365LL*24*60*60)*1'000'000;
    impl_->maximum_visible=std::max<std::size_t>(1,maximum_visible);
    const auto previous=std::atomic_load(&impl_->presentation);
    for(const auto& entry:previous->jobs)PublishPresentationLocked(entry->job,entry->collection);
  }
  Prune();
}

void ReferenceStore::ScheduleLegacyMigrations() {
  std::size_t scheduled=0;
  {
    std::lock_guard lock(impl_->mutex);if(impl_->stopping)return;
    for(const auto& [id,job]:impl_->jobs)
      if(job.state==ReferenceJobState::kIndexing&&impl_->owners.contains(id))
        ++scheduled;
    for(const auto& [id,job]:impl_->jobs) {
      if(scheduled>=2)break;
      if(job.state!=ReferenceJobState::kIndexing||impl_->owners.contains(id))continue;
      ++impl_->owners[id];++scheduled;
      impl_->work.push_back([this,id]{MigrateLegacyReady(id);});
    }
  }
  if(scheduled>0)impl_->work_ready.notify_all();
}

void ReferenceStore::MigrateLegacyReady(std::string id) {
  std::optional<StoredReference> loaded;
  try {loaded=Unpack(ReadFile(directory_/(id+".stref")));}catch(...) {}
  std::string validation_error;
  const bool valid=loaded&&loaded->value.id==id&&
      Validate(loaded->value,&validation_error);
  std::function<Stamp()> clock;ReferenceJobSnapshot migrated;
  {
    std::lock_guard lock(impl_->mutex);clock=impl_->hooks.now;
    const auto found=impl_->jobs.find(id);
    if(found!=impl_->jobs.end())migrated=found->second;
  }
  const auto terminal=clock?clock():Now();
  migrated.terminal_utc_us=migrated.terminal_utc_us>0?
      migrated.terminal_utc_us:terminal.utc_us;
  bool sidecar_ready=false;
  if(valid) {
    migrated.context=loaded->value.context;migrated.path=loaded->value.path;
    migrated.regions=loaded->value.regions;
    migrated.page_identity=loaded->value.page_identity;
    migrated.captured_page=loaded->value.captured_page;
    migrated.key_down_monotonic_ms=0;migrated.key_up_monotonic_ms=0;
    migrated.circle_completed=loaded->value.circle_completed;
    migrated.crop_margin=loaded->value.crop_margin;
    auto metadata=loaded->value;
    metadata.source.bytes.clear();metadata.crop.bytes.clear();
    migrated.metadata=std::make_shared<const Reference>(std::move(metadata));
    bool still_indexing=false;
    {std::lock_guard lock(impl_->mutex);const auto found=impl_->jobs.find(id);
      still_indexing=found!=impl_->jobs.end()&&
          found->second.state==ReferenceJobState::kIndexing;}
    sidecar_ready=still_indexing&&
        PersistAsset(AssetPath(directory_,id,false),loaded->value.source.bytes)&&
        PersistAsset(AssetPath(directory_,id,true),loaded->value.crop.bytes)&&
        PersistSmall(directory_/(id+".ready"),ReadyStateText(migrated));
  }
  if(!valid||!sidecar_ready) {
    const auto code=valid?"migration_failed":"corrupt";
    (void)PersistSmall(directory_/(id+".failed"),TerminalStateText(
        code,migrated.accepted_utc_us,terminal.utc_us));
  }
  bool deleted=false;
  {
    std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
    deleted=found!=impl_->jobs.end()&&
        found->second.state==ReferenceJobState::kDeleted;
    if(found!=impl_->jobs.end()&&found->second.state==ReferenceJobState::kIndexing) {
      if(valid&&sidecar_ready) {
        found->second.state=ReferenceJobState::kReady;found->second.code.clear();
        found->second.context=std::move(migrated.context);
        found->second.path=std::move(migrated.path);
        found->second.regions=std::move(migrated.regions);
        found->second.circle_completed=migrated.circle_completed;
        found->second.crop_margin=migrated.crop_margin;
        found->second.metadata=std::move(migrated.metadata);
      } else {
        found->second.state=ReferenceJobState::kFailed;
        found->second.code=valid?"migration_failed":"corrupt";
        found->second.terminal_utc_us=terminal.utc_us;
      }
      PublishPresentationLocked(found->second,found->second.state==ReferenceJobState::kReady?
          ReadCollection(directory_,*found->second.metadata):ReferenceCollection{});
    }
  }
  if(deleted)(void)CleanupDeletedFiles(id);
  if(valid&&sidecar_ready)unlink((directory_/(id+".failed")).c_str());
  ReleaseOwner(id);
  ScheduleLegacyMigrations();
}

void ReferenceStore::Prune() {
  std::function<Stamp()> clock;
  {std::lock_guard lock(impl_->mutex);clock=impl_->hooks.now;}
  const auto now=clock?clock():Now();
  std::vector<std::string> cleanup;bool changed=false;
  {
    std::lock_guard lock(impl_->mutex);
    std::vector<std::string> terminal;
    for(const auto& [id,job]:impl_->jobs) {
      const auto owner=impl_->owners.find(id);
      if((job.state==ReferenceJobState::kIndexing||
          job.state==ReferenceJobState::kReady||
          job.state==ReferenceJobState::kFailed)&&
         (owner==impl_->owners.end()||owner->second==0))terminal.push_back(id);
    }
    std::sort(terminal.begin(),terminal.end(),[&](const auto& first,const auto& second) {
      const auto& a=impl_->jobs[first];const auto& b=impl_->jobs[second];
      const auto at=a.terminal_utc_us>0?a.terminal_utc_us:a.accepted_utc_us;
      const auto bt=b.terminal_utc_us>0?b.terminal_utc_us:b.accepted_utc_us;
      return at==bt?first>second:at>bt;
    });
    for(std::size_t index=0;index<terminal.size();++index) {
      auto& job=impl_->jobs[terminal[index]];
      const auto retained_since=job.accepted_utc_us;
      const bool too_old=retained_since<=0||
          (now.utc_us>=retained_since&&
           now.utc_us-retained_since>impl_->retention_ttl_us);
      if(index<impl_->maximum_visible&&!too_old)continue;
      job.state=ReferenceJobState::kExpired;job.code="expired_reference";
      job.ready.reset();job.expired_utc_us=now.utc_us;
      impl_->ready_access.erase(job.id);cleanup.push_back(job.id);changed=true;
      PublishPresentationLocked(job,{});
      if(impl_->derived_crop&&impl_->derived_crop->id==job.id&&
         impl_->derived_crop->state!=ReadyAssetState::kPending)
        impl_->derived_crop.reset();
      if(impl_->derived_annotated&&impl_->derived_annotated->id==job.id&&
         impl_->derived_annotated->state!=ReadyAssetState::kPending)
        impl_->derived_annotated.reset();
    }
    std::vector<std::string> expired;
    for(const auto& [id,job]:impl_->jobs)
      if(job.state==ReferenceJobState::kExpired)expired.push_back(id);
    std::sort(expired.begin(),expired.end(),[&](const auto& first,const auto& second) {
      const auto& a=impl_->jobs[first];const auto& b=impl_->jobs[second];
      return a.expired_utc_us==b.expired_utc_us?first>second:
             a.expired_utc_us>b.expired_utc_us;
    });
    for(std::size_t index=0;index<expired.size();++index) {
      const auto found=impl_->jobs.find(expired[index]);
      if(found==impl_->jobs.end())continue;
      const bool tombstone_old=found->second.expired_utc_us<=0||
          (now.utc_us>=found->second.expired_utc_us&&
           now.utc_us-found->second.expired_utc_us>impl_->retention_ttl_us);
      if(index<impl_->maximum_visible&&!tombstone_old)continue;
      cleanup.push_back(found->first);impl_->ready_access.erase(found->first);
      if(impl_->derived_crop&&impl_->derived_crop->id==found->first&&
         impl_->derived_crop->state!=ReadyAssetState::kPending)
        impl_->derived_crop.reset();
      if(impl_->derived_annotated&&impl_->derived_annotated->id==found->first&&
         impl_->derived_annotated->state!=ReadyAssetState::kPending)
        impl_->derived_annotated.reset();
      impl_->jobs.erase(found);changed=true;
    }
    TrimReadyPayloads(*impl_);
    TrimAnnotatedCache(*impl_);
    if(changed) {
      impl_->work.push_back([this,cleanup=std::move(cleanup)] {
        std::lock_guard io_lock(impl_->retention_io_mutex);
        std::ostringstream manifest;
        {
          std::lock_guard lock(impl_->mutex);
          std::vector<ReferenceJobSnapshot> tombstones;
          for(const auto& [id,job]:impl_->jobs) {
            (void)id;if(job.state==ReferenceJobState::kExpired)
              tombstones.push_back(job);
          }
          std::sort(tombstones.begin(),tombstones.end(),[](const auto& a,const auto& b) {
            return a.id<b.id;
          });
          for(const auto& job:tombstones)
            manifest<<job.id<<' '<<job.accepted_utc_us<<' '
                    <<job.terminal_utc_us<<' '<<job.expired_utc_us<<'\n';
        }
        if(!PersistSmall(directory_/".tombstones",manifest.str()))return;
        std::lock_guard lock(impl_->mutex);
        for(const auto& id:cleanup) {
          const auto found=impl_->jobs.find(id);
          if(found!=impl_->jobs.end()&&
             found->second.state!=ReferenceJobState::kExpired)continue;
          unlink((directory_/(id+".pending")).c_str());
          unlink((directory_/(id+".failed")).c_str());
          unlink((directory_/(id+".ready")).c_str());
          unlink((directory_/(id+".stref")).c_str());
          unlink(AssetPath(directory_,id,false).c_str());
          unlink(AssetPath(directory_,id,true).c_str());
          unlink(AnnotatedAssetPath(directory_,id).c_str());
          unlink(AnnotatedDigestPath(directory_,id).c_str());
          unlink(NotesPath(directory_,id).c_str());
          RemoveCollectionFiles(directory_,id);
        }
      });
    }
  }
  if(changed)impl_->work_ready.notify_one();
}

ReferenceJobSnapshot ReferenceStore::AcceptPending(
    Reference reference,std::int64_t key_down_monotonic_ms,
    std::int64_t key_up_monotonic_ms) {
  NormalizeCapturedPage(reference);
  std::function<Stamp()> clock;
  {std::lock_guard lock(impl_->mutex);clock=impl_->hooks.now;}
  const Stamp accepted=clock?clock():Now();
  ReferenceJobSnapshot job;job.id=reference.id;job.context=reference.context;
  job.path=reference.path;job.regions=reference.regions;
  job.page_identity=reference.page_identity;
  job.captured_page=reference.captured_page;
  job.metadata=std::make_shared<const Reference>(reference);
  job.accepted_utc_us=accepted.utc_us;
  job.deadline_utc_us=job.accepted_utc_us+kJobTimeoutUs;
  job.accepted_monotonic_us=accepted.monotonic_us;
  job.deadline_monotonic_us=job.accepted_monotonic_us+kJobTimeoutUs;
  job.key_down_monotonic_ms=key_down_monotonic_ms;
  job.key_up_monotonic_ms=key_up_monotonic_ms;
  job.circle_completed=reference.circle_completed;
  job.crop_margin=reference.crop_margin;
  std::size_t sample_count=job.path.size();
  for(const auto& region:job.regions)
    ForEachSubpath(region, [&](const auto& path) { sample_count += path.size(); });
  const bool invalid=!SafeID(job.id)||job.context.pid<=0||
      EffectiveRegions(reference).empty()||sample_count>10'000||
      (reference.schema!=1&&reference.schema!=2)||key_down_monotonic_ms<0||
      key_up_monotonic_ms<key_down_monotonic_ms||
      (reference.page_identity &&
       (!ValidPageIdentity(*reference.page_identity)||
        !PageIdentityMatchesContext(*reference.page_identity,reference.context)))||
      !ValidCaptureScope(reference.context)||
      (reference.context.scope.policy_present &&
       ((reference.context.scope.kind==CaptureScopeKind::kPage)!=bool(reference.page_identity)))||
      (reference.context.bundle_id=="com.google.Chrome" &&
       !reference.page_identity && !reference.context.scope.policy_present);
  {
    std::lock_guard lock(impl_->mutex);
    if(auto found=impl_->jobs.find(job.id);found!=impl_->jobs.end())return found->second;
    if(invalid) {job.state=ReferenceJobState::kFailed;job.code="invalid";
      job.terminal_utc_us=job.accepted_utc_us;}
    else if(impl_->stopping||impl_->unfinished>=kMaximumUnfinishedJobs||
            impl_->reserved+kPerJobReservation>kMaximumReservedBytes) {
      job.state=ReferenceJobState::kFailed;job.code="busy";
      job.terminal_utc_us=job.accepted_utc_us;
    } else {
      job.state=ReferenceJobState::kPending;job.code="capture";
      ++impl_->unfinished;impl_->reserved+=kPerJobReservation;
      impl_->reservations[job.id]=kPerJobReservation;
    }
    impl_->jobs.emplace(job.id,job);
    PublishPresentationLocked(job,{});
  }
  if(job.state==ReferenceJobState::kPending) {
    {std::lock_guard lock(impl_->mutex);impl_->work.push_back([this,id=job.id,metadata=job.metadata,accepted=job.accepted_utc_us] {
      (void)PersistSmall(directory_/(id+".pending"),
          LifecycleStateText("pending",accepted,0,metadata));
      std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
      if(found==impl_->jobs.end()||found->second.state!=ReferenceJobState::kPending)
        unlink((directory_/(id+".pending")).c_str());
    });}
    impl_->work_ready.notify_one();
  } else Prune();
  return job;
}

void ReferenceStore::FinalizeAsync(std::string id,CaptureResult capture,
                                   Completion completion) {
  {
    std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
    if(found==impl_->jobs.end()||found->second.state!=ReferenceJobState::kPending||
       found->second.code!="capture")return;
    found->second.code="finalizing";
    ++impl_->owners[id];
    impl_->completions[id]=completion;
    impl_->work.push_back([this,id=std::move(id),capture=std::move(capture),
                           completion=std::move(completion)]() mutable {
      const std::shared_ptr<void> release_owner(nullptr,[this,id](void*) {
        ReleaseOwner(id);
      });
      ReferenceStoreTestHooks hooks;Reference reference;
      {std::lock_guard lock(impl_->mutex);hooks=impl_->hooks;
        auto found=impl_->jobs.find(id);if(found==impl_->jobs.end())return;
        if(found->second.metadata)reference=*found->second.metadata;
        else {reference.id=id;reference.mark_id=id;reference.context=found->second.context;
          reference.path=found->second.path;reference.regions=found->second.regions;}
        reference.id=id;reference.mark_id=id;
        reference.circle_completed=found->second.circle_completed;
        reference.crop_margin=found->second.crop_margin;}
      if(hooks.stage)hooks.stage("capture",id);
      if(!capture.error.empty()||!ValidPixels(capture.pixels)) {
        Fail(id,capture.error.empty()?"capture_invalid":"capture_failed",
             std::move(completion));return;
      }
      reference.requested=capture.requested;
      reference.image_completed=capture.completed;
      if(hooks.stage)hooks.stage("finalize",id);
      std::string error;auto stored=FinalizeReference(std::move(reference),
          std::move(capture.pixels),&error,hooks.stage,id);
      if(!stored){Fail(id,error.empty()?"finalize_failed":error,
                       std::move(completion));return;}
      if(hooks.stage)hooks.stage("store",id);
      const auto now=hooks.now?hooks.now().monotonic_us:Now().monotonic_us;
      bool may_commit=false;
      {std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
        if(found!=impl_->jobs.end()&&
           found->second.state==ReferenceJobState::kPending&&
           found->second.code=="finalizing") {
          if(now<found->second.deadline_monotonic_us) {
            found->second.code="committing";may_commit=true;
          }
        }}
      if(!may_commit) {Fail(id,"timeout",std::move(completion));return;}
      SaveResult saved=SaveResult::kIOError;
      if(hooks.fail_save) {
        saved=SaveResult::kIOError;
      } else if(stored->value.schema==2) {
        std::lock_guard io_lock(impl_->retention_io_mutex);
        if(std::filesystem::exists(directory_/(id+".deleted")))
          saved=SaveResult::kDeleted;
        else if(std::filesystem::exists(directory_/(id+".stref"))||
                std::filesystem::exists(directory_/(id+".ready"))||
                std::filesystem::exists(AssetPath(directory_,id,false))||
                std::filesystem::exists(AssetPath(directory_,id,true)))
          saved=SaveResult::kConflict;
        else saved=SaveResult::kSaved;
      } else {
        saved=Save(*stored,&error);
      }
      if(saved==SaveResult::kDeleted) return;
      if(saved!=SaveResult::kSaved&&saved!=SaveResult::kIdempotent) {
        Fail(id,saved==SaveResult::kConflict?"conflict":"store_failed",
             std::move(completion));return;
      }
      if(hooks.stage)hooks.stage("stored",id);
      ReferenceJobSnapshot prepared;
      {std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
        if(found==impl_->jobs.end())return;
        if(found->second.state==ReferenceJobState::kDeleted) {
          prepared=found->second;
        } else if(found->second.state!=ReferenceJobState::kPending) {
          return;
        } else prepared=found->second;}
      if(prepared.state==ReferenceJobState::kDeleted) {
        (void)CleanupDeletedFiles(id);return;
      }
      prepared.terminal_utc_us=stored->value.image_completed.utc_us;
      prepared.context=stored->value.context;prepared.path=stored->value.path;
      prepared.regions=stored->value.regions;
      auto metadata=stored->value;
      metadata.source.bytes.clear();metadata.crop.bytes.clear();
      prepared.metadata=std::make_shared<const Reference>(std::move(metadata));
      if(hooks.stage)hooks.stage("metadata_serialize_begin",id);
      const auto ready_state=ReadyStateText(prepared);
      if(hooks.stage)hooks.stage("metadata_serialize_end",id);
      if(hooks.stage)hooks.stage("assets_begin",id);
      bool assets_ready=PersistAsset(AssetPath(directory_,id,false),
                                     stored->value.source.bytes);
      if(assets_ready&&stored->value.schema==1)
        assets_ready=PersistAsset(AssetPath(directory_,id,true),
                                  stored->value.crop.bytes);
      if(assets_ready&&stored->value.schema==2) {
        if(hooks.stage)hooks.stage("source_readback_begin",id);
        try {
          const auto source_path=AssetPath(directory_,id,false);
          assets_ready=std::filesystem::file_size(source_path)==
              stored->value.source.bytes.size()&&
              Sha256File(source_path)==stored->value.source.sha256;
        } catch(...) {assets_ready=false;}
        if(hooks.stage)hooks.stage("source_readback_end",id);
      }
      if(assets_ready)assets_ready=PersistSmall(directory_/(id+".ready"),ready_state);
      if(!assets_ready) {
        Fail(id,"asset_store_failed",std::move(completion));return;
      }
      if(hooks.stage)hooks.stage("assets_end",id);
      ReferenceJobSnapshot result;
      bool deleted_during_assets=false;
      {std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(id);
        if(found==impl_->jobs.end())return;
        if(found->second.state==ReferenceJobState::kDeleted) {
          deleted_during_assets=true;
        } else if(found->second.state!=ReferenceJobState::kPending) {
          return;
        } else {
        found->second.state=ReferenceJobState::kReady;found->second.code.clear();
        found->second.page_identity=stored->value.page_identity;
        found->second.captured_page=stored->value.captured_page;
        found->second.ready=std::make_shared<const StoredReference>(std::move(*stored));
        found->second.terminal_utc_us=prepared.terminal_utc_us;
        found->second.context=std::move(prepared.context);
        found->second.path=std::move(prepared.path);
        found->second.regions=std::move(prepared.regions);
        found->second.metadata=std::move(prepared.metadata);
        impl_->ready_access[id]=++impl_->access_clock;
        impl_->completions.erase(id);
        PublishPresentationLocked(found->second,ReadCollection(directory_,*found->second.metadata));
        result=found->second;}}
      if(deleted_during_assets) {
        (void)CleanupDeletedFiles(id);return;
      }
      unlink((directory_/(id+".pending")).c_str());
      unlink((directory_/(id+".failed")).c_str());
      if(completion)completion(std::move(result));
    });
  }
  impl_->work_ready.notify_one();
}

void ReferenceStore::Fail(std::string_view id,std::string code,
                          Completion completion) {
  std::function<Stamp()> clock;
  {std::lock_guard lock(impl_->mutex);clock=impl_->hooks.now;}
  const auto terminal=clock?clock():Now();
  ReferenceJobSnapshot result;bool changed=false;
  {std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(std::string(id));
    if(found==impl_->jobs.end()||found->second.state!=ReferenceJobState::kPending)return;
    found->second.state=ReferenceJobState::kFailed;found->second.code=std::move(code);
    found->second.terminal_utc_us=terminal.utc_us;
    found->second.ready.reset();
    const auto owner=impl_->owners.find(found->first);
    if(owner==impl_->owners.end()||owner->second==0) {
      if(auto reservation=impl_->reservations.find(found->first);
         reservation!=impl_->reservations.end()) {
        impl_->reserved-=reservation->second;impl_->reservations.erase(reservation);
        if(impl_->unfinished>0)--impl_->unfinished;
      }
    }
    if(!completion) {
      if(auto callback=impl_->completions.find(found->first);
         callback!=impl_->completions.end()) {
        completion=std::move(callback->second);
        impl_->completions.erase(callback);
      }
    } else {
      impl_->completions.erase(found->first);
    }
    PublishPresentationLocked(found->second,{});
    result=found->second;changed=true;
    impl_->work.push_back([this,job_id=found->first,reason=found->second.code,
                           accepted=found->second.accepted_utc_us,
                           terminal=found->second.terminal_utc_us,metadata=found->second.metadata] {
      (void)PersistSmall(directory_/(job_id+".failed"),
          LifecycleStateText(reason,accepted,terminal,metadata));
      unlink((directory_/(job_id+".pending")).c_str());
      Prune();
    });}
  if(changed){impl_->work_ready.notify_one();if(completion)completion(std::move(result));}
}

bool ReferenceStore::CleanupDeletedFiles(std::string_view id) {
  ReferenceStoreTestHooks hooks;
  {std::lock_guard lock(impl_->mutex);hooks=impl_->hooks;}
  const std::array<std::filesystem::path,9> paths={
      directory_/(std::string(id)+".pending"),
      directory_/(std::string(id)+".failed"),
      directory_/(std::string(id)+".ready"),
      directory_/(std::string(id)+".stref"),
      AssetPath(directory_,id,false),AssetPath(directory_,id,true),
      AnnotatedAssetPath(directory_,id),AnnotatedDigestPath(directory_,id),
      NotesPath(directory_,id)};
  bool success=true;std::lock_guard io_lock(impl_->retention_io_mutex);
  for(const auto& path:paths) {
    bool removed=false;
    if(hooks.remove_owned_file)removed=hooks.remove_owned_file(path);
    else {errno=0;removed=unlink(path.c_str())==0||errno==ENOENT;}
    success=removed&&success;
  }
  success=RemoveCollectionFiles(directory_,id,hooks.remove_owned_file)&&success;
  return success;
}

DeleteResult ReferenceStore::Delete(std::string_view id) {
  if(!SafeID(std::string(id)))return DeleteResult::kNotFound;
  std::function<Stamp()> clock;
  ReferenceJobSnapshot result;Completion completion;bool already=false;
  {
    std::lock_guard lock(impl_->mutex);clock=impl_->hooks.now;
    const auto found=impl_->jobs.find(std::string(id));
    if(found==impl_->jobs.end())return DeleteResult::kNotFound;
    already=found->second.state==ReferenceJobState::kDeleted;
    result=found->second;
  }
  const auto terminal=clock?clock():Now();
  if(!already&&!PersistSmall(directory_/(std::string(id)+".deleted"),
      TerminalStateText("deleted_reference",result.accepted_utc_us,
                        terminal.utc_us)))
    return DeleteResult::kCleanupFailed;
  if(!already) {
    std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(std::string(id));
    if(found==impl_->jobs.end())return DeleteResult::kNotFound;
    if(found->second.state!=ReferenceJobState::kDeleted) {
      found->second.state=ReferenceJobState::kDeleted;
      found->second.code="deleted_reference";
      found->second.terminal_utc_us=terminal.utc_us;
      found->second.ready.reset();found->second.metadata.reset();
      PublishPresentationLocked(found->second,{});
      EraseAnnotatedCacheForId(*impl_,found->first);
      found->second.context={};found->second.path.clear();found->second.regions.clear();
      impl_->ready_access.erase(found->first);
      if(impl_->derived_crop&&impl_->derived_crop->id==found->first&&
         impl_->derived_crop->state!=ReadyAssetState::kPending)
        impl_->derived_crop.reset();
      if(impl_->derived_annotated&&impl_->derived_annotated->id==found->first&&
         impl_->derived_annotated->state!=ReadyAssetState::kPending)
        impl_->derived_annotated.reset();
      if(auto callback=impl_->completions.find(found->first);
         callback!=impl_->completions.end()) {
        completion=std::move(callback->second);impl_->completions.erase(callback);
      }
      const auto owner=impl_->owners.find(found->first);
      if(owner==impl_->owners.end()||owner->second==0)
        if(auto reservation=impl_->reservations.find(found->first);
           reservation!=impl_->reservations.end()) {
          impl_->reserved-=reservation->second;impl_->reservations.erase(reservation);
          if(impl_->unfinished>0)--impl_->unfinished;
        }
      result=found->second;
    } else already=true;
  }
  const bool cleaned=CleanupDeletedFiles(id);
  const std::string code=cleaned?"deleted_reference":"delete_cleanup_failed";
  const bool tombstone_updated=PersistSmall(
      directory_/(std::string(id)+".deleted"),
      TerminalStateText(code,result.accepted_utc_us,result.terminal_utc_us));
  {
    std::lock_guard lock(impl_->mutex);const auto found=impl_->jobs.find(std::string(id));
    if(found!=impl_->jobs.end()&&found->second.state==ReferenceJobState::kDeleted) {
      found->second.code=cleaned&&tombstone_updated?
          "deleted_reference":"delete_cleanup_failed";
      result=found->second;
    }
  }
  if(completion)completion(result);
  if(!cleaned||!tombstone_updated)return DeleteResult::kCleanupFailed;
  return already?DeleteResult::kAlreadyDeleted:DeleteResult::kDeleted;
}

void ReferenceStore::ReleaseOwner(std::string_view id) {
  bool released=false;
  {
    std::lock_guard lock(impl_->mutex);
    auto owner=impl_->owners.find(std::string(id));
    if(owner==impl_->owners.end()||owner->second==0)return;
    if(--owner->second!=0)return;
    const std::string job_id=owner->first;
    impl_->owners.erase(owner);
    const auto job=impl_->jobs.find(job_id);
    if(job==impl_->jobs.end()||job->second.state==ReferenceJobState::kPending)return;
    if(auto reservation=impl_->reservations.find(job_id);
       reservation!=impl_->reservations.end()) {
      impl_->reserved-=reservation->second;impl_->reservations.erase(reservation);
      if(impl_->unfinished>0)--impl_->unfinished;
    }
    released=true;
  }
  if(released)Prune();
}

std::optional<ReferenceJobSnapshot> ReferenceStore::LookupMetadata(
    std::string_view id) const {
  const_cast<ReferenceStore*>(this)->Prune();
  std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(std::string(id));
  if(found==impl_->jobs.end())return {};
  auto result=found->second;result.ready.reset();
  if(result.state==ReferenceJobState::kReady)
    for(const auto& [other_id,other]:impl_->jobs) {
      (void)other_id;if(other.state==ReferenceJobState::kReady&&
        other.terminal_utc_us>result.terminal_utc_us)++result.newer_ready;
    }
  return result;
}

std::optional<Bytes> ReferenceStore::ReadReadyAsset(std::string_view id,
                                                    bool crop) const {
  const auto job=LookupMetadata(id);
  if(!job||job->state!=ReferenceJobState::kReady||!job->metadata)return {};
  const auto& image=crop?job->metadata->crop:job->metadata->source;
  if(crop&&job->metadata->schema==2) {
    {
      std::lock_guard lock(impl_->mutex);
      const auto found=impl_->jobs.find(std::string(id));
      if(found!=impl_->jobs.end()&&found->second.ready&&
         !found->second.ready->value.crop.bytes.empty()) {
        const auto& candidate=found->second.ready->value.crop.bytes;
        if((image.sha256.empty()||Sha256(candidate)==image.sha256))
          return candidate;
      }
    }
    try {
      const auto source_path=AssetPath(directory_,id,false);
      if(std::filesystem::file_size(source_path)>kMaxBytes)return {};
      const auto source=ReadFile(source_path);
      return DerivedCropBytes(*job->metadata,source);
    } catch(...) {return {};}
  }
  try {
    const auto path=AssetPath(directory_,id,crop);
    if(std::filesystem::file_size(path)>kMaxBytes)return {};
    auto bytes=ReadFile(path);
    if(bytes.size()<8||bytes[0]!=137||bytes[1]!=80||bytes[2]!=78||
       bytes[3]!=71||bytes[4]!=13||bytes[5]!=10||bytes[6]!=26||bytes[7]!=10||
       Sha256(bytes)!=image.sha256)return {};
    Image checked=image;checked.bytes=bytes;
    if((image.encoding=="png-zlib-stored-v1"&&!DecodeLegacyPng(checked))||
       !InspectPng(checked,{0,0,double(image.width),double(image.height)},false))
      return {};
    return bytes;
  }catch(...) {return {};}
}

ReadyAssetResult ReferenceStore::RequestReadyCrop(std::string_view id) {
  const auto indexed=LookupMetadata(id);
  if(!indexed||indexed->state!=ReferenceJobState::kReady||!indexed->metadata)
    return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
  if(indexed->metadata->schema!=2) {
    auto bytes=ReadReadyAsset(id,true);
    if(!bytes)return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
    return {ReadyAssetState::kReady,
      std::make_shared<const Bytes>(std::move(*bytes)),{},
      indexed->metadata->crop.sha256};
  }

  const std::string identity(id);
  const std::string binding=indexed->metadata->source.sha256+":"+
      Sha256(Serialize(*indexed->metadata));
  const std::string cache_key=identity+":source-crop-v1:"+binding;
  std::shared_ptr<const StoredReference> live;
  std::shared_ptr<const Reference> metadata;
  bool scheduled=false;
  {
    std::lock_guard lock(impl_->mutex);
    const auto job=impl_->jobs.find(identity);
    if(job==impl_->jobs.end()||job->second.state!=ReferenceJobState::kReady||
       !job->second.metadata||job->second.metadata->schema!=2||
       job->second.metadata!=indexed->metadata)
      return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
    metadata=job->second.metadata;live=job->second.ready;
    if(!live||live->value.crop.bytes.empty()) {
      if(auto cached=impl_->annotated_cache.find(cache_key);
         cached!=impl_->annotated_cache.end()&&
         cached->second.representation==AnnotatedRepresentation::kSourceCrop&&
         cached->second.metadata_binding==binding) {
        cached->second.last_access=++impl_->access_clock;
        return {cached->second.state,cached->second.bytes,
                cached->second.code,cached->second.byte_sha256};
      }
      if(impl_->derived_crop&&impl_->derived_crop->id==identity&&
         impl_->derived_crop->cache_key==cache_key&&
         impl_->derived_crop->metadata_binding==binding&&
         impl_->derived_crop->metadata==metadata)
        return {impl_->derived_crop->state,impl_->derived_crop->bytes,
                impl_->derived_crop->code,impl_->derived_crop->byte_sha256};
      if(impl_->derived_crop&&impl_->derived_crop->state==ReadyAssetState::kPending)
        return {ReadyAssetState::kPending,{},"queued",{}};
      if(impl_->stopping)
        return {ReadyAssetState::kFailed,{},"shutdown",{}};
      // A completed crop requested by metadata must not pin the slot. Preserve
      // its owned bytes in the SAME typed/count/byte-bounded LRU used by the
      // other derived representations; abandoned clients consume no new budget.
      if(impl_->derived_crop&&impl_->derived_crop->state==ReadyAssetState::kReady&&
         impl_->derived_crop->bytes) {
        const auto& previous=*impl_->derived_crop;
        auto& cached=impl_->annotated_cache[previous.cache_key];
        cached=Impl::DerivedAnnotated{};
        cached.id=previous.id;cached.generation=previous.generation;
        cached.state=previous.state;cached.bytes=previous.bytes;
        cached.code=previous.code;cached.byte_sha256=previous.byte_sha256;
        cached.representation=AnnotatedRepresentation::kSourceCrop;
        cached.cache_key=previous.cache_key;cached.metadata_binding=previous.metadata_binding;
        cached.last_access=++impl_->access_clock;TrimAnnotatedCache(*impl_);
      }
      const auto generation=++impl_->derived_crop_generation;
      impl_->derived_crop=Impl::DerivedCrop{
          identity,generation,ReadyAssetState::kPending,{},"deriving",{}};
      impl_->derived_crop->cache_key=cache_key;
      impl_->derived_crop->metadata_binding=binding;impl_->derived_crop->metadata=metadata;
      impl_->work.push_back([this,identity,metadata,generation,cache_key,binding] {
        ReferenceStoreTestHooks hooks;
        {std::lock_guard lock(impl_->mutex);hooks=impl_->hooks;}
        std::optional<Bytes> bytes;
        try {
          if(hooks.before_derive_crop)hooks.before_derive_crop(identity);
          bytes=DerivedCropBytes(*metadata,
                                 ReadFile(AssetPath(directory_,identity,false)));
        } catch(...) {}
        std::lock_guard lock(impl_->mutex);
        if(!impl_->derived_crop||impl_->derived_crop->id!=identity||
           impl_->derived_crop->generation!=generation||
           impl_->derived_crop->cache_key!=cache_key||
           impl_->derived_crop->metadata_binding!=binding||
           impl_->derived_crop->metadata!=metadata)return;
        const auto job=impl_->jobs.find(identity);
        if(job==impl_->jobs.end()||job->second.state!=ReferenceJobState::kReady||
           job->second.metadata!=metadata) {
          impl_->derived_crop.reset();return;
        }
        if(bytes) {
          impl_->derived_crop->state=ReadyAssetState::kReady;
          impl_->derived_crop->byte_sha256=Sha256(*bytes);
          impl_->derived_crop->bytes=
              std::make_shared<const Bytes>(std::move(*bytes));
          impl_->derived_crop->code.clear();
        } else {
          impl_->derived_crop->state=ReadyAssetState::kFailed;
          impl_->derived_crop->code="asset_corrupt";
        }
      });
      scheduled=true;
    }
  }
  if(scheduled) {
    impl_->work_ready.notify_one();
    return {ReadyAssetState::kPending,{},"deriving",{}};
  }
  if(live&&!live->value.crop.bytes.empty()) {
    const auto hash=Sha256(live->value.crop.bytes);
    if(metadata->crop.sha256.empty()||hash==metadata->crop.sha256)
      return {ReadyAssetState::kReady,
        std::make_shared<const Bytes>(live->value.crop.bytes),{},hash};
  }
  return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
}

ReadyAssetResult ReferenceStore::RequestReadyAnnotated(std::string_view id) {
  return RequestAnnotated(id,{});
}
ReadyAssetResult ReferenceStore::RequestReadyDisplayUnderlay(std::string_view id) {
  return RequestAnnotated(id,{},AnnotatedRepresentation::kDisplayUnderlay);
}
ReadyAssetResult ReferenceStore::RequestReadyMark(std::string_view id,
                                                  std::size_t region_index) {
  return RequestAnnotated(id,region_index);
}
MarkNotesSnapshot ReferenceStore::MarkSnapshots(std::string_view id) const {
  MarkNotesSnapshot result;
  if(!SafeID(std::string(id)))return result;
  const auto job=LookupMetadata(id);
  if(!job)return result;
  if(job->state==ReferenceJobState::kDeleted) {
    result.status=MarkNotesStatus::kDeleted;return result;
  }
  if(job->state==ReferenceJobState::kExpired) {
    result.status=MarkNotesStatus::kExpired;return result;
  }
  if(job->state==ReferenceJobState::kFailed) {
    result.status=MarkNotesStatus::kFailed;return result;
  }
  if(!job->metadata) {
    result.status=MarkNotesStatus::kCorrupt;return result;
  }
  if(job->state==ReferenceJobState::kPending) {
    result.status=MarkNotesStatus::kPending;
    const auto regions=EffectiveRegions(*job->metadata);
    for(std::size_t slot=0;slot<regions.size();++slot) {
      const auto anchor=FirstPoint(regions[slot]);if(!anchor)continue;
      result.marks.push_back({StableMarkId(id,slot),slot,static_cast<std::uint32_t>(slot+1),
          *anchor,{},"",true,0,std::string(id),slot});
    }
    return result;
  }
  const auto collection=Collection(id);
  if(collection.status!=MarkNotesStatus::kReady) {result.status=collection.status;return result;}
  return PresentationNotes(id,collection);
}

MarkNoteUpdateResult ReferenceStore::UpdateMarkNote(
    std::string_view id,std::string_view mark_id,std::string note,
    std::uint64_t expected_revision) {
  if(!SafeID(std::string(id))||!SafeID(std::string(mark_id))||
     !ValidNote(note))return {MarkNoteUpdateStatus::kInvalid,0};
  std::lock_guard io_lock(impl_->retention_io_mutex);
  std::lock_guard lock(impl_->mutex);
  const auto found=impl_->jobs.find(std::string(id));
  if(found==impl_->jobs.end())return {MarkNoteUpdateStatus::kNotFound,0};
  if(found->second.state==ReferenceJobState::kDeleted)
    return {MarkNoteUpdateStatus::kDeleted,0};
  if(found->second.state==ReferenceJobState::kExpired)
    return {MarkNoteUpdateStatus::kExpired,0};
  if(found->second.state!=ReferenceJobState::kReady||!found->second.metadata)
    return {MarkNoteUpdateStatus::kInvalid,0};
  if(std::filesystem::exists(CollectionPath(directory_,id))) {
    auto collection=ReadCollection(directory_,*found->second.metadata);
    PublishPresentationLocked(found->second,collection);
    if(collection.status!=MarkNotesStatus::kReady)return {MarkNoteUpdateStatus::kCorrupt,0};
    if(collection.notes_revision!=expected_revision)
      return {MarkNoteUpdateStatus::kRevisionConflict,collection.notes_revision};
    std::size_t slot=collection.notes.size();
    for(std::size_t index=0;index<collection.notes.size();++index)
      if(StableMarkId(id,index)==mark_id){slot=index;break;}
    if(slot==collection.notes.size())return {MarkNoteUpdateStatus::kInvalid,0};
    collection.notes[slot]=std::move(note);
    if(collection.notes_revision==std::numeric_limits<std::uint64_t>::max())return {MarkNoteUpdateStatus::kInvalid,collection.notes_revision};
    ++collection.notes_revision;
    const auto persisted=PersistNotes(CollectionPath(directory_,id),
        EncodeCollection(*found->second.metadata,collection),impl_->hooks.fail_note_after_rename);
    PublishPresentationLocked(found->second,ReadCollection(directory_,*found->second.metadata));
    return {persisted==NotesWriteResult::kSaved?MarkNoteUpdateStatus::kUpdated:
      persisted==NotesWriteResult::kCommitUncertain?MarkNoteUpdateStatus::kCommitUncertain:
      MarkNoteUpdateStatus::kIOError,collection.notes_revision};
  }
  const auto& reference=*found->second.metadata;
  const auto regions=EffectiveRegions(reference);
  if(regions.empty()||regions.size()>1024)
    return {MarkNoteUpdateStatus::kCorrupt,0};
  std::size_t slot=regions.size();
  for(std::size_t index=0;index<regions.size();++index)
    if(StableMarkId(id,index)==mark_id){slot=index;break;}
  if(slot==regions.size())return {MarkNoteUpdateStatus::kInvalid,0};
  auto sidecar=ReadNotes(directory_,reference,regions.size());
  if(sidecar.status!=MarkNotesStatus::kReady)
    PublishPresentationLocked(found->second,ReadCollection(directory_,reference));
  if(sidecar.status==MarkNotesStatus::kCorrupt)
    return {MarkNoteUpdateStatus::kCorrupt,0};
  if(sidecar.status==MarkNotesStatus::kUnsupported)
    return {MarkNoteUpdateStatus::kUnsupported,0};
  if(sidecar.status==MarkNotesStatus::kIOError)
    return {MarkNoteUpdateStatus::kIOError,0};
  if(sidecar.revision!=expected_revision)
    return {MarkNoteUpdateStatus::kRevisionConflict,sidecar.revision};
  if(sidecar.revision==std::numeric_limits<std::uint64_t>::max())
    return {MarkNoteUpdateStatus::kInvalid,sidecar.revision};
  sidecar.notes[slot]=std::move(note);++sidecar.revision;
  const auto bytes=EncodeNotes(reference,sidecar);
  const auto persisted=PersistNotes(NotesPath(directory_,id),bytes,
                                     impl_->hooks.fail_note_after_rename);
  PublishPresentationLocked(found->second,ReadCollection(directory_,*found->second.metadata));
  if(persisted==NotesWriteResult::kIOError)
    return {MarkNoteUpdateStatus::kIOError,expected_revision};
  if(persisted==NotesWriteResult::kCommitUncertain)
    return {MarkNoteUpdateStatus::kCommitUncertain,sidecar.revision};
  return {MarkNoteUpdateStatus::kUpdated,sidecar.revision};
}
MarkNoteUpdateResult ReferenceStore::UpdateMarkVisibility(
    std::string_view id,std::string_view mark_id,bool visible,
    std::uint64_t expected_revision) {
  if(!SafeID(std::string(id))||!SafeID(std::string(mark_id)))
    return {MarkNoteUpdateStatus::kInvalid,0};
  std::lock_guard io_lock(impl_->retention_io_mutex);
  // Keep the lifecycle state stable through publication. Delete and expiry
  // must follow this write and then remove the sidecar, or precede it and win.
  std::lock_guard lock(impl_->mutex);
  const auto found=impl_->jobs.find(std::string(id));
  if(found==impl_->jobs.end())return {MarkNoteUpdateStatus::kNotFound,0};
  if(found->second.state==ReferenceJobState::kDeleted)
    return {MarkNoteUpdateStatus::kDeleted,0};
  if(found->second.state==ReferenceJobState::kExpired)
    return {MarkNoteUpdateStatus::kExpired,0};
  if(found->second.state!=ReferenceJobState::kReady||!found->second.metadata)
    return {MarkNoteUpdateStatus::kInvalid,0};
  if(std::filesystem::exists(CollectionPath(directory_,id))) {
    auto collection=ReadCollection(directory_,*found->second.metadata);
    PublishPresentationLocked(found->second,collection);
    if(collection.status!=MarkNotesStatus::kReady)return {MarkNoteUpdateStatus::kCorrupt,0};
    if(collection.notes_revision!=expected_revision)
      return {MarkNoteUpdateStatus::kRevisionConflict,collection.notes_revision};
    std::size_t slot=collection.notes.size();
    for(std::size_t index=0;index<collection.notes.size();++index)
      if(StableMarkId(id,index)==mark_id){slot=index;break;}
    if(slot==collection.notes.size())return {MarkNoteUpdateStatus::kInvalid,0};
    collection.visible[slot]=visible;
    if(collection.notes_revision==std::numeric_limits<std::uint64_t>::max())return {MarkNoteUpdateStatus::kInvalid,collection.notes_revision};
    ++collection.notes_revision;
    const auto persisted=PersistNotes(CollectionPath(directory_,id),
        EncodeCollection(*found->second.metadata,collection),impl_->hooks.fail_note_after_rename);
    PublishPresentationLocked(found->second,ReadCollection(directory_,*found->second.metadata));
    return {persisted==NotesWriteResult::kSaved?MarkNoteUpdateStatus::kUpdated:
      persisted==NotesWriteResult::kCommitUncertain?MarkNoteUpdateStatus::kCommitUncertain:
      MarkNoteUpdateStatus::kIOError,collection.notes_revision};
  }
  const auto& reference=*found->second.metadata;
  const auto regions=EffectiveRegions(reference);
  if(regions.empty()||regions.size()>1024)
    return {MarkNoteUpdateStatus::kCorrupt,0};
  std::size_t slot=regions.size();
  for(std::size_t index=0;index<regions.size();++index)
    if(StableMarkId(id,index)==mark_id){slot=index;break;}
  if(slot==regions.size())return {MarkNoteUpdateStatus::kInvalid,0};
  auto sidecar=ReadNotes(directory_,reference,regions.size());
  if(sidecar.status!=MarkNotesStatus::kReady)
    PublishPresentationLocked(found->second,ReadCollection(directory_,reference));
  if(sidecar.status==MarkNotesStatus::kCorrupt)
    return {MarkNoteUpdateStatus::kCorrupt,0};
  if(sidecar.status==MarkNotesStatus::kUnsupported)
    return {MarkNoteUpdateStatus::kUnsupported,0};
  if(sidecar.status==MarkNotesStatus::kIOError)
    return {MarkNoteUpdateStatus::kIOError,0};
  if(sidecar.revision!=expected_revision)
    return {MarkNoteUpdateStatus::kRevisionConflict,sidecar.revision};
  if(sidecar.revision==std::numeric_limits<std::uint64_t>::max())
    return {MarkNoteUpdateStatus::kInvalid,sidecar.revision};
  sidecar.visible[slot]=visible;
  ++sidecar.revision;
  const auto persisted=PersistNotes(NotesPath(directory_,id),
      EncodeNotes(reference,sidecar),found->second.state==ReferenceJobState::kReady&&
      impl_->hooks.fail_note_after_rename);
  PublishPresentationLocked(found->second,ReadCollection(directory_,*found->second.metadata));
  if(persisted==NotesWriteResult::kIOError)
    return {MarkNoteUpdateStatus::kIOError,expected_revision};
  if(persisted==NotesWriteResult::kCommitUncertain)
    return {MarkNoteUpdateStatus::kCommitUncertain,sidecar.revision};
  return {MarkNoteUpdateStatus::kUpdated,sidecar.revision};
}
ReadyAssetResult ReferenceStore::RequestAnnotated(
    std::string_view id,std::optional<std::size_t> region_index,
    AnnotatedRepresentation representation) {
  const auto indexed=LookupMetadata(id);
  if(!indexed||indexed->state!=ReferenceJobState::kReady||!indexed->metadata)
    return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
  const auto region_count=indexed->metadata->regions.empty()?
      (indexed->metadata->path.empty()?0U:1U):
      indexed->metadata->regions.size();
  if(region_index&&*region_index>=region_count)
    return {ReadyAssetState::kFailed,{},"invalid_mark",{}};
  bool underlay=representation==AnnotatedRepresentation::kDisplayUnderlay;
  if(underlay&&indexed->metadata->schema!=2)
    return {ReadyAssetState::kFailed,{},"unsupported",{}};
  std::string binding=indexed->metadata->source.sha256+":"+
      Sha256(Serialize(*indexed->metadata));
  std::string identity(id);
  std::string cache_key=underlay?
      identity+":display-underlay:"+std::string(kDisplayUnderlayGeometryVersion)+":"+binding:
      region_index?
      identity+":mark-numbered-v3:"+std::to_string(*region_index):identity;
  std::shared_ptr<const Reference> metadata;
  bool requested_started=false;
  {
    std::lock_guard lock(impl_->mutex);
    const auto job=impl_->jobs.find(identity);
    if(job==impl_->jobs.end()||job->second.state!=ReferenceJobState::kReady||
       !job->second.metadata||job->second.metadata!=indexed->metadata)
      return {ReadyAssetState::kFailed,{},"asset_unavailable",{}};
    const auto remove_waiter=[&] {
      std::erase_if(impl_->annotated_waiters,[&](const auto& w) {return w.cache_key==cache_key;});
    };
    if(auto cached=impl_->annotated_cache.find(cache_key);
       cached!=impl_->annotated_cache.end()&&
       cached->second.metadata_binding==binding&&
       cached->second.representation==representation) {
      remove_waiter();
      cached->second.last_access=++impl_->access_clock;
      return {cached->second.state,cached->second.bytes,
              cached->second.code,cached->second.byte_sha256};
    }
    if(impl_->derived_annotated&&impl_->derived_annotated->id==identity&&
       impl_->derived_annotated->region_index==region_index&&
       impl_->derived_annotated->cache_key==cache_key&&
       impl_->derived_annotated->metadata_binding==binding&&
       impl_->derived_annotated->representation==representation) {
      remove_waiter();
      return {impl_->derived_annotated->state,
              impl_->derived_annotated->bytes,
              impl_->derived_annotated->code,
              impl_->derived_annotated->byte_sha256};
    }
    // FIFO descriptors contain no image bytes. Any cold poll advances the oldest
    // valid request, even if a metadata-only requester never comes back. A ready
    // result moves to the same bounded LRU rather than blocking the active slot.
    std::erase_if(impl_->annotated_waiters,[&](const auto& w) {
      const auto found=impl_->jobs.find(w.id);
      return found==impl_->jobs.end()||found->second.state!=ReferenceJobState::kReady||
             found->second.metadata!=w.metadata;
    });
    Impl::AnnotatedWaiter requested{identity,cache_key,binding,region_index,
                                    representation,job->second.metadata};
    const bool already_queued=std::any_of(impl_->annotated_waiters.begin(),
        impl_->annotated_waiters.end(),[&](const auto& w) {return w.cache_key==cache_key;});
    if(impl_->derived_annotated&&
       impl_->derived_annotated->state==ReadyAssetState::kPending) {
      if(!already_queued&&impl_->annotated_waiters.size()<32)
        impl_->annotated_waiters.push_back(std::move(requested));
      return {ReadyAssetState::kPending,{},"queued",{}};
    }
    auto next=requested;
    if(!impl_->annotated_waiters.empty()) {
      // Drain before capacity admission: even32 abandoned waiters must advance
      // when a new client polls. Popping first leaves room without growing32.
      next=std::move(impl_->annotated_waiters.front());
      impl_->annotated_waiters.pop_front();
      if(!already_queued&&next.cache_key!=cache_key)
        impl_->annotated_waiters.push_back(std::move(requested));
    }
    requested_started=next.cache_key==cache_key;
    identity=std::move(next.id);cache_key=std::move(next.cache_key);
    binding=std::move(next.metadata_binding);region_index=next.region_index;
    representation=next.representation;metadata=std::move(next.metadata);
    underlay=representation==AnnotatedRepresentation::kDisplayUnderlay;
    if(impl_->stopping)
      return {ReadyAssetState::kFailed,{},"shutdown",{}};
    if(impl_->derived_annotated&&
       impl_->derived_annotated->state==ReadyAssetState::kReady&&
       impl_->derived_annotated->bytes) {
      const auto& previous=*impl_->derived_annotated;
      const std::string& previous_key=previous.cache_key;
      auto& cached=impl_->annotated_cache[previous_key];
      cached=*impl_->derived_annotated;
      cached.last_access=++impl_->access_clock;
      TrimAnnotatedCache(*impl_);
    }
    const auto generation=++impl_->derived_annotated_generation;
    impl_->derived_annotated=Impl::DerivedAnnotated{
        identity,region_index,generation,ReadyAssetState::kPending,{},"deriving",{}};
    impl_->derived_annotated->representation=representation;
    impl_->derived_annotated->cache_key=cache_key;
    impl_->derived_annotated->metadata_binding=binding;
    impl_->work.push_back([this,identity,metadata,generation,region_index,representation,cache_key,binding,underlay] {
      ReferenceStoreTestHooks hooks;
      {std::lock_guard lock(impl_->mutex);hooks=impl_->hooks;}
      std::optional<Bytes> bytes;
      bool generated=false;
      try {
        if(hooks.before_derive_annotated)hooks.before_derive_annotated(identity);
        const auto annotated_path=AnnotatedAssetPath(directory_,identity);
        const auto digest_path=AnnotatedDigestPath(directory_,identity);
        if(!underlay&&!region_index&&std::filesystem::exists(annotated_path)&&
           std::filesystem::exists(digest_path)&&
           std::filesystem::file_size(annotated_path)<=kMaxBytes&&
           std::filesystem::file_size(digest_path)<=256) {
          auto cached=ReadFile(annotated_path);
          const auto digest=Sha256(cached);
          const auto persisted=ReadFile(digest_path);
          if(std::string(persisted.begin(),persisted.end())==
             AnnotatedDigestText(*metadata,digest)) {
            Image image=metadata->source;
            image.bytes=cached;image.sha256=digest;
            if(InspectPng(image,{0,0,double(image.width),
                                    double(image.height)},false))
              bytes=std::move(cached);
          }
        }
      } catch(...) {}
      if(!bytes) try {
          const auto source_path=AssetPath(directory_,identity,false);
          if(std::filesystem::file_size(source_path)<=kMaxBytes) {
            bytes=AnnotatedBytes(*metadata,ReadFile(source_path),region_index,!underlay);
            generated=bytes.has_value();
          }
      } catch(...) {}
      if(generated&&bytes&&!underlay&&!region_index) {
        std::lock_guard io_lock(impl_->retention_io_mutex);
        bool still_ready=false;
        {
          std::lock_guard lock(impl_->mutex);
          const auto job=impl_->jobs.find(identity);
          still_ready=impl_->derived_annotated&&
              impl_->derived_annotated->id==identity&&
              impl_->derived_annotated->region_index==region_index&&
              impl_->derived_annotated->generation==generation&&
              impl_->derived_annotated->cache_key==cache_key&&
              impl_->derived_annotated->metadata_binding==binding&&
              impl_->derived_annotated->representation==representation&&
              job!=impl_->jobs.end()&&job->second.state==ReferenceJobState::kReady&&
              job->second.metadata==metadata;
        }
        if(still_ready&&
           (!PersistAsset(AnnotatedAssetPath(directory_,identity),*bytes)||
            !PersistSmall(AnnotatedDigestPath(directory_,identity),
                          AnnotatedDigestText(*metadata,Sha256(*bytes)))))
          bytes.reset();
        if(!still_ready)bytes.reset();
      }
      std::lock_guard lock(impl_->mutex);
      if(!impl_->derived_annotated||
         impl_->derived_annotated->id!=identity||
         impl_->derived_annotated->region_index!=region_index||
         impl_->derived_annotated->generation!=generation||
         impl_->derived_annotated->cache_key!=cache_key||
         impl_->derived_annotated->metadata_binding!=binding||
         impl_->derived_annotated->representation!=representation)return;
      const auto job=impl_->jobs.find(identity);
      if(job==impl_->jobs.end()||job->second.state!=ReferenceJobState::kReady||
         job->second.metadata!=metadata) {
        impl_->derived_annotated.reset();return;
      }
      if(bytes) {
        impl_->derived_annotated->state=ReadyAssetState::kReady;
        impl_->derived_annotated->byte_sha256=Sha256(*bytes);
        impl_->derived_annotated->bytes=
            std::make_shared<const Bytes>(std::move(*bytes));
        impl_->derived_annotated->code.clear();
      } else {
        impl_->derived_annotated->state=ReadyAssetState::kFailed;
        impl_->derived_annotated->code="asset_corrupt";
      }
    });
  }
  impl_->work_ready.notify_one();
  return {ReadyAssetState::kPending,{},requested_started?"deriving":"queued",{}};
}

std::optional<ReferenceJobSnapshot> ReferenceStore::Lookup(std::string_view id) const {
  auto metadata=LookupMetadata(id);if(!metadata)return {};
  if(metadata->state!=ReferenceJobState::kReady)return metadata;
  ReferenceJobSnapshot result;
  {
    std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(std::string(id));
    if(found==impl_->jobs.end())return {};
    if(found->second.state!=ReferenceJobState::kReady)return found->second;
    if(found->second.ready) {
      impl_->ready_access[found->first]=++impl_->access_clock;
      result=found->second;TrimReadyPayloads(*impl_);
    } else result=found->second;
  }
  if(result.state==ReferenceJobState::kReady&&!result.ready) {
    std::shared_ptr<const StoredReference> ready;
    if(result.metadata&&result.metadata->schema==2) {
      const auto source=ReadReadyAsset(result.id,false);
      const auto crop=ReadReadyAsset(result.id,true);
      if(source&&crop) {
        auto value=*result.metadata;value.source.bytes=*source;value.crop.bytes=*crop;
        if(Validate(value))ready=std::make_shared<const StoredReference>(
            Canonical(value));
      }
    } else try {
        auto loaded=Unpack(ReadFile(directory_/(result.id+".stref")));
        if(loaded&&loaded->value.id==result.id)
          ready=std::make_shared<const StoredReference>(std::move(*loaded));
      } catch(...) {}
    if(ready) {
      std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(result.id);
      if(found==impl_->jobs.end())return {};
      if(found->second.state!=ReferenceJobState::kReady)return found->second;
      found->second.ready=std::move(ready);
      // Hydrating an immutable payload must not erase a failed read-time
      // consistency gate established from duplicate sidecar Contexts.
      if(!found->second.metadata)found->second.context=found->second.ready->value.context;
      found->second.path=found->second.ready->value.path;
      found->second.regions=found->second.ready->value.regions;
      found->second.page_identity=found->second.ready->value.page_identity;
      impl_->ready_access[found->first]=++impl_->access_clock;
      result=found->second;TrimReadyPayloads(*impl_);
    } else if(result.metadata && result.context.bundle_id!="com.google.Chrome" &&
              !CaptureProvenanceSupported(*result.metadata)) {
      // Readable metadata/notes are still history when capture evidence cannot
      // satisfy live presentation. Do not convert title rejection into deletion
      // or an inaccessible failed job merely because full hydration is invalid.
      return result;
    } else {
      const auto terminal=Now().utc_us;
      std::lock_guard lock(impl_->mutex);auto found=impl_->jobs.find(result.id);
      if(found==impl_->jobs.end())return {};
      if(found->second.state!=ReferenceJobState::kReady)return found->second;
      found->second.state=ReferenceJobState::kFailed;found->second.code="corrupt";
      found->second.terminal_utc_us=terminal;found->second.ready.reset();
      const_cast<ReferenceStore*>(this)->PublishPresentationLocked(found->second,{});
      impl_->ready_access.erase(found->first);result=found->second;
    }
  }
  result.newer_ready=metadata->newer_ready;
  return result;
}

std::vector<ReferenceJobSnapshot> ReferenceStore::Jobs() const {
  const_cast<ReferenceStore*>(this)->Prune();
  std::lock_guard lock(impl_->mutex);std::vector<ReferenceJobSnapshot> result;
  result.reserve(impl_->jobs.size());for(const auto& [id,job]:impl_->jobs) {
    (void)id;result.push_back(job);}
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
    return a.accepted_utc_us==b.accepted_utc_us?a.id<b.id:
           a.accepted_utc_us<b.accepted_utc_us;});return result;
}

std::vector<ReferenceJobSnapshot> ReferenceStore::MetadataJobs() const {
  auto result=Jobs();
  for(auto& job:result)job.ready.reset();
  return result;
}

void ReferenceStore::SetTestHooks(ReferenceStoreTestHooks hooks) {
  std::lock_guard lock(impl_->mutex);
  std::atomic_store(&impl_->presentation_now,hooks.now?
      std::make_shared<const std::function<Stamp()>>(hooks.now):std::shared_ptr<const std::function<Stamp()>>{});
  impl_->hooks=std::move(hooks);
}

void ReferenceStore::WaitForIdleForTesting() {
  std::unique_lock lock(impl_->mutex);impl_->idle.wait(lock,[&]{
    return impl_->work.empty()&&impl_->active==0;});
}
}  // namespace seethis::core
