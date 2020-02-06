SUBDIRS := basic monitoring datagram simple_fec multipath westwood no_pacing loss_monitor

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@

.PHONY: all $(SUBDIRS)
