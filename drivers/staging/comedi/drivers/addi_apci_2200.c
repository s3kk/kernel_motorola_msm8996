#include "../comedidev.h"
#include "addi_watchdog.h"

/*
 * I/O Register Map
 */
#define APCI2200_DI_REG			0x00
#define APCI2200_DO_REG			0x04
#define APCI2200_WDOG_REG		0x08

static int apci2200_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inw(dev->iobase + APCI2200_DI_REG);

	return insn->n;
}

static int apci2200_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inw(dev->iobase + APCI2200_DO_REG);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outw(s->state, dev->iobase + APCI2200_DO_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int apci2200_reset(struct comedi_device *dev)
{
	outw(0x0, dev->iobase + APCI2200_DO_REG);

	addi_watchdog_reset(dev->iobase + APCI2200_WDOG_REG);

	return 0;
}

static int apci2200_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret, n_subdevices;

	dev->board_name = dev->driver->driver_name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 1);

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci2200_di_insn_bits;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci2200_do_insn_bits;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	ret = addi_watchdog_init(s, dev->iobase + APCI2200_WDOG_REG);
	if (ret)
		return ret;

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	s->type = COMEDI_SUBD_UNUSED;

	/* EEPROM */
	s = &dev->subdevices[6];
	s->type = COMEDI_SUBD_UNUSED;

	apci2200_reset(dev);
	return 0;
}

static void apci2200_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		apci2200_reset(dev);
	if (dev->subdevices)
		addi_watchdog_cleanup(&dev->subdevices[4]);
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci2200_driver = {
	.driver_name	= "addi_apci_2200",
	.module		= THIS_MODULE,
	.auto_attach	= apci2200_auto_attach,
	.detach		= apci2200_detach,
};

static int apci2200_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci2200_driver);
}

static void apci2200_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci2200_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1005) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci2200_pci_table);

static struct pci_driver apci2200_pci_driver = {
	.name		= "addi_apci_2200",
	.id_table	= apci2200_pci_table,
	.probe		= apci2200_pci_probe,
	.remove		= apci2200_pci_remove,
};
module_comedi_pci_driver(apci2200_driver, apci2200_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
