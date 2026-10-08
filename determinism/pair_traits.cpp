#include <utility>
#include <cstdint>
#include <cstdio>
#include <type_traits>
int main(){
  using P1 = std::pair<uint32_t,float>; using P2 = std::pair<int,int>;
  printf("pair<u32,float>: triv_copy=%d triv_default=%d sizeof=%zu\n", std::is_trivially_copyable_v<P1>, std::is_trivially_default_constructible_v<P1>, sizeof(P1));
  printf("pair<int,int>: triv_copy=%d triv_default=%d\n", std::is_trivially_copyable_v<P2>, std::is_trivially_default_constructible_v<P2>);
}
