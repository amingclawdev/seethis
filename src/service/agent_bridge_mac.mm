#import <Foundation/Foundation.h>
#include "service/agent_bridge.h"
#include "core/reference.h"
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace seethis::service {
namespace {
bool Id(std::string_view id) {
  return id.size()==32 && id.find_first_not_of("0123456789abcdef")==id.npos;
}
bool OwnedFile(NSString* path, bool private_file) {
  if(![path isKindOfClass:NSString.class]||![path isAbsolutePath])return false;
  struct stat info{};
  return lstat(path.fileSystemRepresentation,&info)==0 && S_ISREG(info.st_mode) &&
      info.st_uid==getuid() && !(info.st_mode&(private_file?0077:0022));
}
std::string Unavailable(const char* reason) {
  return std::string("{\"available\":false,\"reason\":\"")+reason+"\"}";
}
}
std::string ReadAgentRuns(std::string_view reference, std::string_view run,
                         const std::filesystem::path& binding) {
#ifdef SEETHIS_COMPILED_SETTINGS_ASSETS
  return Unavailable("plugin_bridge_unavailable_in_sandbox_build");
#else
  if(!Id(reference)||(!run.empty()&&!Id(run)))return Unavailable("invalid_identity");
  @autoreleasepool {
    @try {
      NSString* config=binding.empty()?[NSHomeDirectory() stringByAppendingPathComponent:@".local/share/seethis-agent-demo/bridge.json"]:
          [NSString stringWithUTF8String:binding.c_str()];
      if(!OwnedFile(config,true))return Unavailable("plugin_not_connected");
      NSData* data=[NSData dataWithContentsOfFile:config];
      if(!data||data.length>32768)return Unavailable("invalid_plugin_binding");
      NSDictionary* spec=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
      if(![spec isKindOfClass:NSDictionary.class]||![spec[@"schema_version"] isEqual:@1])return Unavailable("invalid_plugin_binding");
      NSString* helper=spec[@"helper"];NSString* python=spec[@"python"];
      NSDictionary* files=spec[@"files"];
      if(!OwnedFile(helper,false)||![helper.lastPathComponent isEqual:@"bridge.py"]||
          ![[helper.stringByDeletingLastPathComponent lastPathComponent] isEqual:@"seethis_runs"]||
          ![python isKindOfClass:NSString.class]||!python.isAbsolutePath||access(python.fileSystemRepresentation,X_OK)!=0||
          ![files isKindOfClass:NSDictionary.class]||files.count<4||files.count>32||!files[helper])
        return Unavailable("invalid_plugin_binding");
      NSString* directory=helper.stringByDeletingLastPathComponent;
      for(NSString* file in files) {
        NSString* expected=files[file];
        if(!OwnedFile(file,false)||![file.stringByDeletingLastPathComponent isEqual:directory]||
            ![file.pathExtension isEqual:@"py"]||![expected isKindOfClass:NSString.class])return Unavailable("invalid_plugin_binding");
        NSData* source=[NSData dataWithContentsOfFile:file];
        if(!source||source.length>1024*1024)return Unavailable("plugin_source_changed");
        const auto* first=static_cast<const std::uint8_t*>(source.bytes);
        if(core::Sha256(core::Bytes(first,first+source.length))!=expected.UTF8String)return Unavailable("plugin_source_changed");
      }
      // Reject newly added Python modules not covered by the explicit binding.
      for(NSString* name in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:directory error:nil])
        if([name.pathExtension isEqual:@"py"]&&!files[[directory stringByAppendingPathComponent:name]])
          return Unavailable("plugin_source_changed");
      NSTask* task=[[NSTask alloc] init];task.executableURL=[NSURL fileURLWithPath:python];
      NSMutableArray* args=[NSMutableArray arrayWithArray:@[@"-I",@"-B",helper,@"--reference",
          [[NSString alloc] initWithBytes:reference.data() length:reference.size() encoding:NSUTF8StringEncoding]]];
      if(!run.empty())[args addObjectsFromArray:@[@"--run",[[NSString alloc] initWithBytes:run.data() length:run.size() encoding:NSUTF8StringEncoding]]];
      task.arguments=args;task.currentDirectoryURL=[NSURL fileURLWithPath:directory];
      NSPipe* output=[NSPipe pipe];task.standardOutput=output;
      task.standardError=[NSFileHandle fileHandleWithNullDevice];task.standardInput=[NSFileHandle fileHandleWithNullDevice];
      NSError* error=nil;if(![task launchAndReturnError:&error])return Unavailable("plugin_launch_failed");
      [output.fileHandleForWriting closeFile];
      const int fd=output.fileHandleForReading.fileDescriptor;
      fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
      std::string result;bool eof=false,limited=false;
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      while(!eof) {
        char buffer[16384];const auto count=read(fd,buffer,sizeof(buffer));
        if(count>0)result.append(buffer,static_cast<std::size_t>(count));
        else if(count==0)eof=true;
        else if(errno!=EAGAIN&&errno!=EINTR){limited=true;break;}
        if(result.size()>8*1024*1024||std::chrono::steady_clock::now()>deadline){limited=true;break;}
        if(count<0)std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if(limited&&task.running)kill(task.processIdentifier,SIGKILL);
      // EOF is not proof that the child exited; it can close stdout early.
      while(task.running&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
      if(task.running){kill(task.processIdentifier,SIGKILL);limited=true;}
      [task waitUntilExit];[output.fileHandleForReading closeFile];
      if(limited||task.terminationStatus!=0)return Unavailable("plugin_read_timed_out_or_failed");
      NSData* bytes=[NSData dataWithBytes:result.data() length:result.size()];
      id value=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
      if(![value isKindOfClass:NSDictionary.class]||![value[@"available"] isKindOfClass:NSNumber.class])return Unavailable("invalid_plugin_response");
      return result;
    } @catch(NSException*) { return Unavailable("plugin_read_failed"); }
  }
#endif
}
}
