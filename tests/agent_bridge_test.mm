#import <Foundation/Foundation.h>
#include "service/agent_bridge.h"
#include "core/reference.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char** argv) {
  @autoreleasepool {
    if(argc==2&&std::string_view(argv[1])=="--installed") {
      const auto decode=[](const std::string& value)->NSDictionary* {
        return [NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:value.data() length:value.size()] options:0 error:nil];
      };
      const std::string reference="458b32d75b4f42078eb3bf650e1c38a9";
      NSDictionary* data=decode(seethis::service::ReadAgentRuns(reference));
      if(![data[@"available"] boolValue]){std::cerr<<"Installed metadata reader unavailable\n";return 1;}
      NSArray* runs=data[@"runs"];std::size_t count=0;
      for(NSDictionary* run in runs) {
        NSDictionary* log=decode(seethis::service::ReadAgentRuns(reference,[run[@"run_id"] UTF8String]));
        if(![log[@"available"] boolValue]){std::cerr<<"Installed bounded log reader unavailable\n";return 1;}
        count+=[log[@"messages"] count];
      }
      std::cout<<"Installed native-to-Python bridge: "<<runs.count<<" registered runs, "<<count<<" readable messages; no bodies retained\n";return 0;
    }
    char name[]="/tmp/seethis-agent-bridge-XXXXXX";
    const std::filesystem::path root=mkdtemp(name),package=root/"seethis_runs",config=root/"bridge.json";
    std::filesystem::create_directory(package);
    const std::string ref(32,'a'),run(32,'b');int checks=0;
    const auto check=[&](bool good,const char* label){++checks;if(!good)throw std::runtime_error(label);};
    const auto bind=[&](const std::string& code) {
      std::ofstream(package/"bridge.py")<<code;
      for(auto name:{"__init__.py","store.py","log_reader.py"})std::ofstream(package/name)<<"# fixture\n";
      NSMutableDictionary* hashes=[NSMutableDictionary dictionary];
      for(const auto& item:std::filesystem::directory_iterator(package)) {
        std::ifstream in(item.path(),std::ios::binary);seethis::core::Bytes bytes{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
        hashes[[NSString stringWithUTF8String:item.path().c_str()]]=[NSString stringWithUTF8String:seethis::core::Sha256(bytes).c_str()];
      }
      NSDictionary* spec=@{@"schema_version":@1,@"python":@SEETHIS_TEST_PYTHON,
        @"helper":[NSString stringWithUTF8String:(package/"bridge.py").c_str()],@"files":hashes};
      NSData* data=[NSJSONSerialization dataWithJSONObject:spec options:0 error:nil];
      [data writeToFile:[NSString stringWithUTF8String:config.c_str()] atomically:YES];chmod(config.c_str(),0600);
    };
    try {
      check(seethis::service::ReadAgentRuns(ref,run,config).find("plugin_not_connected")!=std::string::npos,"missing connection");
      bind("import json,sys\nprint(json.dumps({'available':True,'args':sys.argv[1:]}))\n");
      auto result=seethis::service::ReadAgentRuns(ref,run,config);
      check(result.find(ref)!=std::string::npos&&result.find(run)!=std::string::npos,"bounded process exact argv");
      check(seethis::service::ReadAgentRuns("../x",run,config).find("invalid_identity")!=std::string::npos,"reject arbitrary path");
      std::ofstream(package/"store.py")<<"# changed\n";
      check(seethis::service::ReadAgentRuns(ref,run,config).find("plugin_source_changed")!=std::string::npos,"source change requires reconnect");
      bind("import json\nprint(json.dumps({'available':True}))\n");chmod(config.c_str(),0644);
      check(seethis::service::ReadAgentRuns(ref,run,config).find("plugin_not_connected")!=std::string::npos,"binding must be private");
      bind("print('not json')\n");
      check(seethis::service::ReadAgentRuns(ref,run,config).find("invalid_plugin_response")!=std::string::npos,"invalid helper output");
      bind("import time\ntime.sleep(10)\n");auto start=std::chrono::steady_clock::now();
      check(seethis::service::ReadAgentRuns(ref,run,config).find("timed_out_or_failed")!=std::string::npos,"timeout is unavailable");
      check(std::chrono::steady_clock::now()-start<std::chrono::seconds(4),"timeout bounded");
      std::filesystem::remove_all(root);std::cout<<checks<<" agent bridge checks passed\n";return 0;
    } catch(const std::exception& error) {std::filesystem::remove_all(root);std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
  }
}
