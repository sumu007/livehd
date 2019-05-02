//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "fmt/format.h"

#include "absl/container/flat_hash_map.h"
#include "mmap_map.hpp"
#include "rng.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_mmap_map_test : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
  }
};

TEST_F(Setup_mmap_map_test, string_index) {
  Rng rng(123);

  bool zero_found = false;
  while(!zero_found) {
    mmap_map::unordered_map<std::string_view,uint32_t> map;
    map.clear();
    absl::flat_hash_map<std::string, uint32_t> map2;

    int conta = 0;
    for(int i=0;i<10000;i++) {
      int sz = rng.uniform<int>(0xFFFF);
      std::string sz_str = std::to_string(sz)+"foo";
      std::string_view key{sz_str};

      if (map.has(key)) {
        EXPECT_EQ(map2.count(key),1);
        continue;
      }

      conta++;

      EXPECT_TRUE(!map.has(key));
      map[key] = sz;
      EXPECT_TRUE(map.has(key));

      EXPECT_EQ(map2.count(key),0);
      map2[key] = sz;
      EXPECT_EQ(map2.count(key),1);
    }

    for(auto it:map) {
      (void)it;
      if(it.getFirst() == 0)
        zero_found = true;

      EXPECT_EQ(map.get_sview(it), std::to_string(it.second) + "foo");
      EXPECT_EQ(map2.count(map.get_sview(it)), 1);
      conta--;
    }
    for(auto it:map2) {
      (map.has(it.first));
      if (!map.has(it.first))
        fmt::print("bug bug\n");
      EXPECT_TRUE(map.has(it.first));
    }

    EXPECT_EQ(conta,0);

    fmt::print("load_factor:{} conflict_factor:{} txt_size:{}\n",map.load_factor(), map.conflict_factor(),map.txt_size());
  }
}

TEST_F(Setup_mmap_map_test, big_entry) {
  Rng rng(123);

  struct Big_entry {
    int f0;
    int f1;
    int f2;
    int f3;
    bool operator==(const Big_entry &o) const {
      return f0 == o.f0
          && f1 == o.f1
          && f2 == o.f2
          && f3 == o.f3;
    }
  };

  mmap_map::unordered_map<uint32_t,Big_entry> map("mmap_map_test_se");
  absl::flat_hash_map<uint32_t,Big_entry> map2;
	auto cap = map.capacity();

	for(auto i=0;i<1000;i++) {
		map.clear();
		map.clear(); // 2 calls to clear triggers a delete to map file

		EXPECT_EQ(map.has(33),0);
		EXPECT_EQ(map.has(0),0);
		map[0].f1=33;
		EXPECT_EQ(map.has(0),1);
		map.erase(0);

		EXPECT_EQ(map.capacity(),cap); // No capacity degeneration

		int conta = 0;
		for(int i=1;i<rng.uniform<int>(16);++i) {
			int sz = rng.uniform<int>(0xFFFFFF);
			if (map.find(sz)!=map.end()) {
				map.erase(sz);
			}else{
				map[sz].f1=sz;
				EXPECT_EQ(map.has(sz),1);
				conta++;
			}
		}

		for(auto it:map) {
			EXPECT_EQ(it.first, it.second.f1);
			conta--;
		}
		EXPECT_EQ(conta,0);
	}
  map.clear();

  int conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);

    if (map2.find(sz) == map2.end()) {
      EXPECT_EQ(map.has(sz),0);
    }else{
      EXPECT_EQ(map.has(sz),1);
    }

    map[sz].f0 = sz;
    map[sz].f1 = sz+1;
    map[sz].f2 = sz+2;
    map[sz].f3 = sz+3;

    if (map2.find(sz) == map2.end()) {
      conta++;
      map2[sz] = map[sz];
    }
  }

  for(auto it:map) {
    EXPECT_EQ(it.first + 0, it.second.f0);
    EXPECT_EQ(it.first + 1, it.second.f1);
    EXPECT_EQ(it.first + 2, it.second.f2);
    EXPECT_EQ(it.first + 3, it.second.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }
  EXPECT_EQ(conta,0);

  fmt::print("load_factor:{} conflict_factor:{}\n",map.load_factor(), map.conflict_factor());

  map.clear();
  for(auto it:map) {
    (void)it;
    EXPECT_TRUE(false);
  }

}

class Big_entry {
public:
  int f0;
  int f1;
  int f2;
  int f3;
  bool operator==(const Big_entry &o) const {
    return f0 == o.f0
        && f1 == o.f1
        && f2 == o.f2
        && f3 == o.f3;
  }
  bool operator!=(const Big_entry &o) const {
    return f0 != o.f0
        || f1 != o.f1
        || f2 != o.f2
        || f3 != o.f3;
  };

  template <typename H>
  friend H AbslHashValue(H h, const Big_entry &s) {
    return H::combine(std::move(h), s.f0, s.f1, s.f2, s.f3);
  };
};

namespace mmap_map {
template <>
struct hash<Big_entry> {
  size_t operator()(Big_entry const &o) const {
    uint32_t h = o.f0;
    h = (h<<2) ^ o.f1;
    h = (h<<2) ^ o.f2;
    h = (h<<2) ^ o.f3;
    return hash<uint32_t>{}(h);
  }
};
}

TEST_F(Setup_mmap_map_test, big_key) {
  Rng rng(123);

  mmap_map::unordered_map<Big_entry,uint32_t> map("mmap_map_test_be");
	map.clear(); // Remove data from previous runs
  absl::flat_hash_map<Big_entry, uint32_t> map2;

  int conta = 0;
  for(int i=0;i<10000;i++) {
    int sz = rng.uniform<int>(0xFFFFFF);
    Big_entry key;
    key.f0 = sz;
    key.f1 = sz+1;
    key.f2 = sz+2;
    key.f3 = sz+3;

    map[key] = sz;

    if (map2.find(key) == map2.end()) {
      conta++;
      map2[key] = sz;
    }
  }

  for(auto it:map) {
    EXPECT_EQ(it.second + 0, it.first.f0);
    EXPECT_EQ(it.second + 1, it.first.f1);
    EXPECT_EQ(it.second + 2, it.first.f2);
    EXPECT_EQ(it.second + 3, it.first.f3);
    EXPECT_TRUE(map2[it.first] == it.second);
    conta--;
  }

  EXPECT_EQ(conta,0);

  fmt::print("load_factor:{} conflict_factor:{}\n",map.load_factor(), map.conflict_factor());
}

