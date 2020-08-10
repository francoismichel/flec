CLANG?=clang-6.0
LLC?=llc-6.0
export CLANG LLC
SUBDIRS := basic monitoring datagram simple_fec multipath westwood qlog no_pacing loss_monitor

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -j$(nproc) -C $@

.PHONY: all $(SUBDIRS)
