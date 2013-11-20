SENDER_DIR = Sender
RECEIVER_DIR = Receiver
.PHONY: all  
all:
	$(MAKE) -C $(SENDER_DIR) && $(MAKE) -C $(RECEIVER_DIR)

clean:
	$(MAKE) -C $(SENDER_DIR) clean && $(MAKE) -C $(RECEIVER_DIR) clean
