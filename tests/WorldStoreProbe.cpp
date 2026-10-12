#include <Jade/World/Streaming.hpp>
#include <iostream>

int main(int argc,char* argv[]) {
  try {
    if (argc != 3) return 2;
    Jade::FileWorldStore store(argv[2]);
    const std::string command=argv[1];
    if(command=="write") {
      Jade::WorldState state{"process-world",{}};
      state.objects.emplace(17,Jade::ObjectState{{9,8},{17,"ball","{\"version\":1,\"velocity\":[2,3,4]}",true,true},false});
      state.objects.emplace(23,Jade::ObjectState{{8,8},{23,"ball","{}",true,true},true});
      store.save(state);
    } else if(command=="read") {
      const auto state=store.load("process-world");
      if(state.objects.size()!=2 || state.objects.at(17).coord!=Jade::ChunkCoord{9,8} ||
          !state.objects.at(23).deleted || state.objects.at(17).data.state.find("velocity")==std::string::npos) return 3;
    } else return 2;
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
