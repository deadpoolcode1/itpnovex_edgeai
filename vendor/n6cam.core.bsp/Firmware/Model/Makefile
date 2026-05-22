
FLASHER = STM32_Programmer_CLI
EL = "$(shell dirname "$(shell which $(FLASHER))")/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"

.PHONY: flash_weights
flash_weights: network_data.hex
	$(FLASHER) -c port=SWD mode=HOTPLUG ap=1 -el $(EL) -w $<
