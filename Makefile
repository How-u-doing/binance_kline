CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra
EXTRA_CXXFLAGS := -O2 # -Wno-template-id-cdtor -g
CMAKE := cmake

YYJSON_INCLUDE := third_party/yyjson/src
YYJSON_BUILD_DIR := third_party/yyjson/build

WEBSOCKETPP_INCLUDE := third_party/websocketpp


.PHONY: all clean

all: yyjson get_kline_data shm_bbuffer_spmc_kline shm_bbuffer_spmc_test

get_kline_data: src/get_kline_data.cc
	$(CXX) -o $@ src/get_kline_data.cc $(CXXFLAGS) $(EXTRA_CXXFLAGS) \
		-I$(WEBSOCKETPP_INCLUDE) \
		-I$(YYJSON_INCLUDE) -L$(YYJSON_BUILD_DIR) -lyyjson -lssl -lcrypto

shm_bbuffer_spmc_kline: src/shm_bbuffer_spmc_kline.cc src/shm_bbuffer_spmc.h
	$(CXX) -o $@ src/shm_bbuffer_spmc_kline.cc $(CXXFLAGS) $(EXTRA_CXXFLAGS) \
		-I$(WEBSOCKETPP_INCLUDE) \
		-I$(YYJSON_INCLUDE) -L$(YYJSON_BUILD_DIR) -lyyjson -lssl -lcrypto

shm_bbuffer_spmc_test: src/shm_bbuffer_spmc_test.cc src/shm_bbuffer_spmc.h
	$(CXX) -o $@ src/shm_bbuffer_spmc_test.cc $(CXXFLAGS) -g

yyjson:
	$(CMAKE) third_party/yyjson -B $(YYJSON_BUILD_DIR)
	$(MAKE) -C $(YYJSON_BUILD_DIR)

install_boost_openssl:
	sudo apt install libssl-dev libboost-all-dev

check_huge_pages_info:
	cat /proc/meminfo | grep Huge

# make N=128 huge pages available in the system
# and put your group id in `hugetlb_shm_group`
enable_huge_pages:
	echo 128 | sudo tee /proc/sys/vm/nr_hugepages
	echo `id -g $(shell whoami)` | sudo tee /proc/sys/vm/hugetlb_shm_group

clean:
	rm -rf *.o get_kline_data shm_bbuffer_spmc_kline shm_bbuffer_spmc_test
