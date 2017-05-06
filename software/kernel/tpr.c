//---------------------------------------------------------------------------------
// Title         : Kernel Module For PCI-Express EVR Card
// Project       : PCI-Express EVR
//---------------------------------------------------------------------------------
// File          : pcie_evr.c
// Author        : Ryan Herbst, rherbst@slac.stanford.edu
// Created       : 05/18/2010
//---------------------------------------------------------------------------------
//
//---------------------------------------------------------------------------------
// Copyright (c) 2010 by SLAC National Accelerator Laboratory. All rights reserved.
//---------------------------------------------------------------------------------
// Modification history:
// 05/18/2010: created.
// 10/13/2015: Modified to support unlocked_ioctl if available
//             Added (irq_handler_t) cast in request_irq
//---------------------------------------------------------------------------------
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/cdev.h>
#include "tpr.h"

#ifndef SA_SHIRQ
/* No idea which version this changed in! */
#define SA_SHIRQ IRQF_SHARED
#endif

// Function prototypes
int     tpr_open     (struct inode *inode, struct file *filp);
int     tpr_release  (struct inode *inode, struct file *filp);
ssize_t tpr_write    (struct file *filp, const char *buf, size_t count, loff_t *f_pos);
ssize_t tpr_read     (struct file *filp, char *buf, size_t count, loff_t *f_pos);
#ifdef HAVE_UNLOCKED_IOCTL
long    tpr_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
int     tpr_ioctl    (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif
irqreturn_t tpr_intr (int irq, void *dev_id, struct pt_regs *regs);
int     tpr_probe    (struct pci_dev *pcidev, const struct pci_device_id *dev_id);
void    tpr_remove   (struct pci_dev *pcidev);
int     tpr_init     (void);
void    tpr_exit     (void);
uint    tpr_poll     (struct file *filp, poll_table *wait );
int     tpr_mmap     (struct file *filp, struct vm_area_struct *vma);
int     tpr_fasync   (int fd, struct file *filp, int mode);
void    tpr_vmopen   (struct vm_area_struct *vma);
void    tpr_vmclose  (struct vm_area_struct *vma);
int     tpr_vmfault  (struct vm_area_struct *vma, struct vm_fault *vmf);
#ifdef CONFIG_COMPAT
long tpr_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif

// PCI device IDs
static struct pci_device_id tpr_ids[] = {
  { PCI_DEVICE(0x1A4A, 0x2011) },  // SLAC TPR
  { 0, }
};

MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, tpr_ids);
module_init(tpr_init);
module_exit(tpr_exit);


// PCI driver structure
static struct pci_driver tprDriver = {
  .name     = MOD_NAME,
  .id_table = tpr_ids,
  .probe    = tpr_probe,
  .remove   = tpr_remove,
};

// Define interface routines
struct file_operations tpr_intf = {
   read:    tpr_read,
   write:   tpr_write,
#ifdef HAVE_UNLOCKED_IOCTL
   unlocked_ioctl: tpr_unlocked_ioctl,
#else
   ioctl:   tpr_ioctl,
#endif
#ifdef CONFIG_COMPAT
  compat_ioctl: tpr_compat_ioctl,
#endif

   open:    tpr_open,
   release: tpr_release,
   poll:    tpr_poll,
   fasync:  tpr_fasync,
   mmap:    tpr_mmap,
};

// Virtual memory operations
static struct vm_operations_struct tpr_vmops = {
  open:  tpr_vmopen,
  close: tpr_vmclose,
  fault: tpr_vmfault
};

static int allocBar(struct bar_dev* minor, int major, struct pci_dev* dev, int bar);

// Open Returns 0 on success, error code on failure
int tpr_open(struct inode *inode, struct file *filp) {
  struct tpr_dev *   dev;
  struct TprReg*     reg;
  int                minor;

  // Extract structure for card
  dev = container_of(inode->i_cdev, struct tpr_dev, cdev);
  minor = iminor(inode);

  printk(KERN_WARNING"%s: Open: Minor %i.  Maj %i\n",
	 MOD_NAME, minor, dev->major);

  if (minor < MOD_SHARED) {
    if (dev->shared[minor].parent) {
      printk(KERN_WARNING"%s: Open: module open failed.  Device already open. Maj=%i, Min=%i.\n",
             MOD_NAME, dev->major, (unsigned)minor);
      return (ERROR);
    }

    dev->minors = dev->minors | (1<<minor);
    dev->shared[minor].parent = dev;
    filp->private_data = &dev->shared[minor];

    //
    //  Enable the dma for this channel
    //
    reg = (struct TprReg*)(dev->bar[0].reg);
    reg->channel[minor].control = reg->channel[minor].control | (1<<2);
    reg->irqControl = 1;
  }
  else if (minor == MOD_SHARED) {
    if (dev->master.parent) {
      printk(KERN_WARNING"%s: Open: module open failed.  Device already open. Maj=%i, Min=%i.\n",
             MOD_NAME, dev->major, (unsigned)minor);
      return (ERROR);
    }

    dev->master.parent = dev;
    filp->private_data = &dev->master;
  }
  else {
    printk(KERN_WARNING"%s: Open: module open failed.  Minor number out of range. Maj=%i, Min=%i.\n",
           MOD_NAME, dev->major, (unsigned)minor);
    return (ERROR);
  }

  return SUCCESS;
}


// tpr_release
// Called when the device is closed
// Returns 0 on success, error code on failure
int tpr_release(struct inode *inode, struct file *filp) {
  int i;
  struct shared_tpr *shared = (struct shared_tpr*)filp->private_data;
  struct TprReg* reg;
  struct tpr_dev *dev;

  if (!shared->parent) {
    printk("%s: Release: module close failed. Already closed.\n",MOD_NAME);
    return ERROR;
  }

  if (shared->idx < 0) {
    dev = (struct tpr_dev*)shared->parent;
  }
  else {
    //
    //  Disable the dma for this channel
    //
    i = shared->idx;
    reg = (struct TprReg*)shared->parent->bar[0].reg;
    reg->channel[i].control = reg->channel[0].control & ~(1<<2);

    dev = (struct tpr_dev*)shared->parent;
    dev->minors = dev->minors & ~(1<<shared->idx);
  }

  printk("%s: Release: Major %u: irqEnable %u, irqDisable %u, irqCount %u, irqNoReq %u\n",
	   MOD_NAME, shared->parent->major,
	   dev->irqEnable,
	   dev->irqDisable,
	   dev->irqCount,
	   dev->irqNoReq);

  printk("%s: Release: Major %u: dmaCount %u, dmaEvent %u, dmaBsaChan %u, dmaBsaCtrl %u\n",
	   MOD_NAME, shared->parent->major,
	   dev->dmaCount,
	   dev->dmaEvent,
	   dev->dmaBsaChan,
	   dev->dmaBsaCtrl);

  //  Unlink
  shared->parent = NULL;


  return SUCCESS;
}


// tpr_write
// noop.
ssize_t tpr_write(struct file *filp, const char *buffer, size_t count, loff_t *f_pos) {
  return(0);
}


// tpr_read
// Returns bit mask of queues with data pending.
ssize_t tpr_read(struct file *filp, char *buffer, size_t count, loff_t *f_pos) 
{
  ssize_t retval = 0;
  struct shared_tpr *shared = ((struct shared_tpr *) filp->private_data);
  __u32 pendingirq;

  do {
    if (count < sizeof(pendingirq))
      break;
    while (!shared->pendingirq) {
      if (filp->f_flags & O_NONBLOCK)
        return -EAGAIN;
      if (wait_event_interruptible(shared->waitq, shared->pendingirq))
        return -ERESTARTSYS;
    }
    pendingirq = test_and_clear_bit(0, (volatile unsigned long*)&shared->pendingirq);
    if (copy_to_user(buffer, &pendingirq, sizeof(pendingirq))) {
      retval = -EFAULT;
      break;
    }
    *f_pos = *f_pos + sizeof(pendingirq);
    retval = sizeof(pendingirq);
  } while(0);

  return retval;
}


// tpr_ioctl
#ifdef HAVE_UNLOCKED_IOCTL
long tpr_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
#else
int tpr_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg) {
#endif

  // lcls-i ioctls only?

  return(ERROR);
}

#ifdef CONFIG_COMPAT
long tpr_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
#ifdef HAVE_UNLOCKED_IOCTL
  return tpr_unlocked_ioctl(file, cmd, arg);
#else
  return tpr_ioctl(NULL, file, cmd, arg);
#endif   
}
#endif

// Bottom half of IRQ Handler
static void tpr_handle_dma(unsigned long arg)
{
  struct tpr_dev* dev = &gDevices[arg];
  struct TprQueues* tprq = dev->amem;

  struct RxBuffer*  next;
  __u32*            dptr;
  __u32             mtyp, ich, mch, wmask=0;

  next = dev->rxPend;

  //  Check the "dma done" bit.
  while (test_and_clear_bit(31, (volatile unsigned long*)next->buffer)) {

    dptr = (__u32*)next->buffer;

    while( ((dptr[0]>>16)&0xf) != END_TAG ) {

      dev->dmaCount++;

      //  Check if a drop preceded us
      if ( dptr[0] & (0x808<<20)) {
        //  How to propagate this?
        //  Write a drop message to all channels?
        tprq->fifofull = 1;  // ??
      }

      //  Check the message type
      mtyp = (dptr[0]>>16)&0xf;
      if ( mtyp>2 ) {  //  Unknown message
        printk(KERN_WARNING "%s: handle unknown msg %08x:%08x\n", MOD_NAME, dptr[0], dptr[1]);
        break;
      }
      else if ( mtyp==BSAEVNT_TAG ) {  
        dev->dmaBsaChan++;
        //  BSA channel frames are targeted to one readout channel.
        //  Append the frame and bump the write pointer
        ich = (dptr[0]>>0)&0xf;
        memcpy(&tprq->chnq[ich].entry[tprq->chnwp[ich]++ & (MAX_TPR_CHNQ-1)], dptr, BSAEVNT_MSGSZ);
        wmask = wmask | (1<<ich);
        dptr += BSAEVNT_MSGSZ>>2;
      }
      else {
        //  Append the frame and bump the write pointer
        //  Timing frames and BSA control frames are targeted to multiple readout channel.
        //  Update the indices for each targeted channel
        if (mtyp==EVENT_TAG) {
          dev->dmaEvent++;
          mch = (dptr[0]>>0)&((1<<MOD_SHARED)-1);
          if (((dptr[1]<<2)+8)!=EVENT_MSGSZ) {
            printk(KERN_WARNING "%s: unexpected event dma size %08x(%08x)...truncating.\n", MOD_NAME, EVENT_MSGSZ,(dptr[1]<<2)+8);
            break;
          }
          memcpy(&tprq->allq[tprq->gwp & (MAX_TPR_ALLQ-1)], dptr, EVENT_MSGSZ);
          dptr += EVENT_MSGSZ>>2;
        }
        else {
          dev->dmaBsaCtrl++;
          mch = (1<<MOD_SHARED)-1;  // BSA control
          memcpy(&tprq->allq[tprq->gwp & (MAX_TPR_ALLQ-1)], dptr, BSACNTL_MSGSZ);
          dptr += BSACNTL_MSGSZ>>2;
        }
        wmask = wmask | mch;
        for( ich=0; mch; ich++) {
          if (mch & (1<<ich)) {
            mch = mch & ~(1<<ich);
            tprq->allrp[ich].idx[ tprq->allwp[ich] & (MAX_TPR_ALLQ-1) ] = tprq->gwp;
            tprq->allwp[ich]++;
          }
        }

        tprq->gwp++;
      }
    }
    
    //  Queue the dma buffer back to the hardware
    ((struct TprReg*)dev->bar[0].reg)->rxFree[0] = next->dma;
    
    next = (struct RxBuffer*)next->lh.next;
  }

  dev->rxPend = next;

  //  Wake the apps
  for( ich=0; ich<MOD_SHARED; ich++) {
    if ((wmask&(1<<ich)) && dev->shared[ich].parent) {
      set_bit(0, (volatile unsigned long*)&dev->shared[ich].pendingirq);
      wake_up(&dev->shared[ich].waitq);
    }
  }

  //  Enable the interrupt
  if (dev->minors)
    ((struct TprReg*)dev->bar[0].reg)->irqControl = 1;
}


// IRQ Handler
irqreturn_t tpr_intr(int irq, void *dev_id, struct pt_regs *regs) {
  unsigned int stat;
  unsigned int handled=0;

  struct tpr_dev *dev = (struct tpr_dev *)dev_id;

  //
  //  Handle the interrupt: 
  //  wakeup the tasklet that copies the dma data into the sw queues
  //
  stat = ((struct TprReg*)dev->bar[0].reg)->irqStatus;
  if ( (stat & 1) != 0 ) {
    // Disable interrupts
    dev->irqCount++;
    dev->irqDisable++;
    if (((struct TprReg*)dev->bar[0].reg)->irqControl==0)
      dev->irqNoReq++;
    ((struct TprReg*)dev->bar[0].reg)->irqControl = 0;
    tasklet_schedule(&dev->dma_task);
    handled=1;
  }

  if (handled==0) return(IRQ_NONE);

  return(IRQ_HANDLED);
}

uint tpr_poll(struct file *filp, poll_table *wait ) {
  struct shared_tpr *dev = (struct shared_tpr *)filp->private_data;

  poll_wait(filp, &(dev->waitq), wait);

  if (dev->pendingirq & 1)
    return(POLLIN | POLLRDNORM); // Readable

  return(0);
}



// Probe device
int tpr_probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   int i, idx, res;
   dev_t chrdev = 0;
   struct tpr_dev* dev;
   struct TprReg*  tprreg;
   struct pci_device_id *id = (struct pci_device_id *) dev_id;

   // We keep device instance number in id->driver_data
   id->driver_data = -1;

   // Find empty structure
   for (i = 0; i < MAX_PCI_DEVICES; i++) {
     if (gDevices[i].bar[0].baseHdwr == 0) {
       id->driver_data = i;
       break;
     }
   }

   // Overflow
   if (id->driver_data < 0) {
     printk(KERN_WARNING "%s: Probe: Too Many Devices.\n", MOD_NAME);
     return -EMFILE;
   }
   dev = &gDevices[id->driver_data];

   dev->qmem = (void *)vmalloc(sizeof(struct TprQueues) + PAGE_SIZE); // , GFP_KERNEL);
   if (!dev->qmem) {
     printk(KERN_WARNING MOD_NAME ": could not allocate %lu.\n", sizeof(struct TprQueues) + PAGE_SIZE);
     return -ENOMEM;
   }

   printk(KERN_WARNING MOD_NAME ": Allocated %lu at %p.\n", sizeof(struct TprQueues) + PAGE_SIZE, dev->qmem);
   memset(dev->qmem, 0, sizeof(struct TprQueues) + PAGE_SIZE);
   dev->amem = (void *)((long)(dev->qmem + PAGE_SIZE - 1) & PAGE_MASK);
   ((struct TprQueues*) dev->amem)->fifofull = 0xabadcafe;

   printk(KERN_WARNING MOD_NAME ": amem = %p.\n", dev->amem);

   // Allocate device numbers for character device.
   res = alloc_chrdev_region(&chrdev, 0, MOD_MINORS, MOD_NAME);
   if (res < 0) {
     printk(KERN_WARNING "%s: Probe: Cannot register char device\n", MOD_NAME);
     return res;
   }

   // Initialize device structure
   dev->major           = MAJOR(chrdev);
   cdev_init(&dev->cdev, &tpr_intf);
   dev->cdev.owner      = THIS_MODULE;
   dev->bar[0].baseHdwr = 0;
   dev->bar[0].baseLen  = 0;
   dev->bar[0].reg      = 0;
   dev->dma_task.func   = tpr_handle_dma;
   dev->dma_task.data   = i;
   dev->minors          = 0;
   dev->irqEnable       = 0;
   dev->irqDisable      = 0;
   dev->irqCount        = 0;
   dev->irqNoReq        = 0;
   dev->dmaCount        = 0;
   dev->dmaEvent        = 0;
   dev->dmaBsaChan      = 0;
   dev->dmaBsaCtrl      = 0;

   // Add device
   if ( cdev_add(&dev->cdev, chrdev, MOD_MINORS) ) 
     printk(KERN_WARNING "%s: Probe: Error adding device Maj=%i\n", MOD_NAME, dev->major);

   // Enable devices
   if (pci_enable_device(pcidev)) {
     printk(KERN_WARNING "%s: Could not enable device \n", MOD_NAME);
     return (ERROR);
   } 
   
   if (allocBar(&dev->bar[0], dev->major, pcidev, 0) == ERROR)
     return (ERROR);

   // Get IRQ from pci_dev structure. 
   dev->irq = pcidev->irq;
   printk(KERN_WARNING "%s: Init: IRQ %d Maj=%i\n", MOD_NAME, dev->irq, dev->major);

   for( i = 0; i < MOD_SHARED; i++) {
     dev->shared[i].parent = NULL;
     dev->shared[i].idx = i;
     init_waitqueue_head(&dev->shared[i].waitq);
     spin_lock_init     (&dev->shared[i].lock);
   }

   dev->master.parent = NULL;
   dev->master.idx    = -1;
   init_waitqueue_head(&dev->master.waitq);
   spin_lock_init     (&dev->master.lock);

   // Device initialization
   tprreg = (struct TprReg* )(dev->bar[0].reg);

   printk(KERN_WARNING "%s: Init: FpgaVersion %08x Maj=%i\n", 
          MOD_NAME, tprreg->FpgaVersion, dev->major);

   tprreg->xbarOut[2] = 1;  // Set LCLS-II timing input

   tprreg->irqControl = 0;  // Disable interrupts

   for( i=0; i<12; i++) {
     tprreg->trigger[i].control=0;  // Disable all channels
   }
   tprreg->trigMaster=1;  // Set LCLS-II mode

   // FIFO size for detecting DMA complete
   tprreg->rxFifoSize = NUMBER_OF_RX_BUFFERS-1;
   tprreg->rxMaxFrame = BUF_SIZE | (1<<31);

   // Init RX Buffers
   dev->rxBuffer   = (struct RxBuffer **) vmalloc(NUMBER_OF_RX_BUFFERS * sizeof(struct RxBuffer *));

   for ( idx=0; idx < NUMBER_OF_RX_BUFFERS; idx++ ) {
     dev->rxBuffer[idx] = (struct RxBuffer *) vmalloc(sizeof(struct RxBuffer ));
     if ((dev->rxBuffer[idx]->buffer = pci_alloc_consistent(pcidev, BUF_SIZE, &(dev->rxBuffer[idx]->dma))) == NULL ) {
       printk(KERN_WARNING"%s: Init: unable to allocate rx buffer [%d/%d]. Maj=%i\n",
              MOD_NAME, idx, NUMBER_OF_RX_BUFFERS, dev->major);
       break;
     }

     clear_bit(31,(volatile unsigned long*)dev->rxBuffer[idx]->buffer);

     // Add to RX queue
     if (idx == 0) {
       dev->rxFree = dev->rxBuffer[idx];
       INIT_LIST_HEAD(&dev->rxFree->lh);
     }
     else
       list_add_tail( &dev->rxBuffer[idx]->lh, 
                      &dev->rxFree->lh );
     tprreg->rxFree[0] = dev->rxBuffer[idx]->dma;
   }

   dev->rxPend = dev->rxFree;

   // Request IRQ from OS.
   if (request_irq(dev->irq, (irq_handler_t) tpr_intr, SA_SHIRQ, MOD_NAME, dev) < 0 ) {
     printk(KERN_WARNING"%s: Open: Unable to allocate IRQ. Maj=%i", MOD_NAME, dev->major);
     return (ERROR);
   }

   printk("%s: Init: Driver is loaded. Maj=%i\n", MOD_NAME,dev->major);
   return SUCCESS;
}


void tpr_remove(struct pci_dev *pcidev) {
   int  i, idx;
   struct tpr_dev *dev = NULL;
   struct TprReg*  tprreg;

   // Look for matching device
   for (i = 0; i < MAX_PCI_DEVICES; i++) {
     if ( gDevices[i].bar[0].baseHdwr == pci_resource_start(pcidev, 0)) {
       dev = &gDevices[i];
       break;
     }
   }

   // Device not found
   if (dev == NULL) {
     printk(KERN_WARNING "%s: Remove: Device Not Found.\n", MOD_NAME);
   }
   else {
     tprreg = (struct TprReg*)dev->bar[0].reg;
     //  Clear the registers
     for( i=0; i<12; i++) {
       tprreg->channel[i].control=0;  // Disable event selection, DMA
       tprreg->trigger[i].control=0;  // Disable TTL
     }

     //  Free all rx buffers awaiting read (TBD)
     for ( idx=0; idx < NUMBER_OF_RX_BUFFERS; idx++ ) {
       if (dev->rxBuffer[idx]->dma != 0) {
         pci_free_consistent( pcidev, BUF_SIZE, dev->rxBuffer[idx]->buffer, dev->rxBuffer[idx]->dma);
         if (dev->rxBuffer[idx]) {
           vfree(dev->rxBuffer[idx]);
         }
       }
     }
     vfree(dev->rxBuffer);
     vfree(dev->qmem);

     // Unmap
     iounmap(dev->bar[0].reg);

     // Release memory region
     release_mem_region(dev->bar[0].baseHdwr, dev->bar[0].baseLen);

     // Release IRQ
     free_irq(dev->irq, dev);

     // Unregister Device Driver
     cdev_del(&dev->cdev);
     unregister_chrdev_region(MKDEV(dev->major,0), MOD_MINORS);

     // Disable device
     pci_disable_device(pcidev);
     dev->bar[0].baseHdwr = 0;
     printk(KERN_ALERT"%s: Remove: Driver is unloaded. Maj=%i\n", MOD_NAME, dev->major);
   }
 }


 // Memory map
int tpr_mmap(struct file *filp, struct vm_area_struct *vma) 
{
   struct shared_tpr *shared = (struct shared_tpr *)filp->private_data;

   unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
   unsigned long vsize  = vma->vm_end - vma->vm_start;
   unsigned long physical;

   int result;

   if (shared->idx < 0) {
     if (vsize > shared->parent->bar[0].baseLen) {
       printk(KERN_WARNING"%s: Mmap: mmap vsize %08x, baseLen %08x. Maj=%i\n", MOD_NAME,
              (unsigned int) vsize, (unsigned int) shared->parent->bar[0].baseLen, shared->parent->major);
       return -EINVAL;
     }
     physical = ((unsigned long) shared->parent->bar[0].baseHdwr) + offset;
     result = io_remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT,
                                 vsize, vma->vm_page_prot);
     if (result) return -EAGAIN;
   }
   else {
     if (vsize > TPR_SH_MEM_WINDOW) {
       printk(KERN_WARNING"%s: Mmap: mmap vsize %08x, baseLen %08x. Maj=%i\n", MOD_NAME,
              (unsigned int) vsize, (unsigned int) shared->parent->bar[0].baseLen, shared->parent->major);
       return -EINVAL;
     }
     /* Handled by tpr_vmfault */
   }

   vma->vm_ops = &tpr_vmops;
   vma->vm_private_data = shared->parent;
   tpr_vmopen(vma);
   return 0;  
}


void tpr_vmopen(struct vm_area_struct *vma) 
{
  struct tpr_dev* dev = vma->vm_private_data;
  dev->vmas++;
}


void tpr_vmclose(struct vm_area_struct *vma) 
{
  struct tpr_dev* dev = vma->vm_private_data;
  dev->vmas--;
}

int tpr_vmfault(struct vm_area_struct* vma,
                struct vm_fault* vmf)
{
  struct tpr_dev* dev = vma->vm_private_data;
  void* pageptr;

  pageptr = dev->amem + (vmf->pgoff << PAGE_SHIFT);

  vmf->page = vmalloc_to_page(pageptr);

  get_page(vmf->page);

  return SUCCESS;
}

 // Flush queue
int tpr_fasync(int fd, struct file *filp, int mode) {
   struct shared_tpr *shared = (struct shared_tpr *)filp->private_data;
   return fasync_helper(fd, filp, mode, &(shared->parent->async_queue));
}

int allocBar(struct bar_dev* minor, int major, struct pci_dev* pcidev, int bar)
{
   // Get Base Address of registers from pci structure.
   minor->baseHdwr = pci_resource_start (pcidev, bar);
   minor->baseLen  = pci_resource_len   (pcidev, bar);
   printk(KERN_WARNING"%s: Init: Alloc bar %i [%lu/%lu].\n", MOD_NAME, bar,
	  minor->baseHdwr, minor->baseLen);

   request_mem_region(minor->baseHdwr, minor->baseLen, MOD_NAME);
   printk(KERN_WARNING "%s: Probe: Found card. Bar%d. Maj=%i\n", 
	  MOD_NAME, bar, major);

   // Remap the I/O register block so that it can be safely accessed.
   minor->reg = ioremap_nocache(minor->baseHdwr, minor->baseLen);
   if (! minor->reg ) {
     printk(KERN_WARNING"%s: Init: Could not remap memory Maj=%i.\n", MOD_NAME,major);
     return (ERROR);
   }

   return SUCCESS;
}

 // Init Kernel Module
int tpr_init(void) {

   /* Allocate and clear memory for all devices. */
   memset(gDevices, 0, sizeof(struct tpr_dev)*MAX_PCI_DEVICES);

   printk(KERN_WARNING"%s: Init: tpr init.\n", MOD_NAME);

   // Register driver
   return(pci_register_driver(&tprDriver));
}


 // Exit Kernel Module
void tpr_exit(void) {
   printk(KERN_WARNING"%s: Exit: tpr exit.\n", MOD_NAME);
   pci_unregister_driver(&tprDriver);
}

