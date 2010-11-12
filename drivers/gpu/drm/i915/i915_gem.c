/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pci.h>

struct change_domains {
	uint32_t invalidate_domains;
	uint32_t flush_domains;
	uint32_t flush_rings;
};

static int i915_gem_object_flush_gpu_write_domain(struct drm_i915_gem_object *obj,
						  struct intel_ring_buffer *pipelined);
static void i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj);
static void i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj);
static int i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj,
					     bool write);
static int i915_gem_object_set_cpu_read_domain_range(struct drm_i915_gem_object *obj,
						     uint64_t offset,
						     uint64_t size);
static void i915_gem_object_set_to_full_cpu_read_domain(struct drm_i915_gem_object *obj);
static int i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
					  bool interruptible);
static int i915_gem_object_bind_to_gtt(struct drm_i915_gem_object *obj,
				       unsigned alignment,
				       bool map_and_fenceable);
static void i915_gem_clear_fence_reg(struct drm_i915_gem_object *obj);
static int i915_gem_phys_pwrite(struct drm_device *dev,
				struct drm_i915_gem_object *obj,
				struct drm_i915_gem_pwrite *args,
				struct drm_file *file);
static void i915_gem_free_object_tail(struct drm_i915_gem_object *obj);

static int i915_gem_inactive_shrink(struct shrinker *shrinker,
				    int nr_to_scan,
				    gfp_t gfp_mask);


/* some bookkeeping */
static void i915_gem_info_add_obj(struct drm_i915_private *dev_priv,
				  size_t size)
{
	dev_priv->mm.object_count++;
	dev_priv->mm.object_memory += size;
}

static void i915_gem_info_remove_obj(struct drm_i915_private *dev_priv,
				     size_t size)
{
	dev_priv->mm.object_count--;
	dev_priv->mm.object_memory -= size;
}

static void i915_gem_info_add_gtt(struct drm_i915_private *dev_priv,
				  struct drm_i915_gem_object *obj)
{
	dev_priv->mm.gtt_count++;
	dev_priv->mm.gtt_memory += obj->gtt_space->size;
	if (obj->gtt_offset < dev_priv->mm.gtt_mappable_end) {
		dev_priv->mm.mappable_gtt_used +=
			min_t(size_t, obj->gtt_space->size,
			      dev_priv->mm.gtt_mappable_end - obj->gtt_offset);
	}
	list_add_tail(&obj->gtt_list, &dev_priv->mm.gtt_list);
}

static void i915_gem_info_remove_gtt(struct drm_i915_private *dev_priv,
				     struct drm_i915_gem_object *obj)
{
	dev_priv->mm.gtt_count--;
	dev_priv->mm.gtt_memory -= obj->gtt_space->size;
	if (obj->gtt_offset < dev_priv->mm.gtt_mappable_end) {
		dev_priv->mm.mappable_gtt_used -=
			min_t(size_t, obj->gtt_space->size,
			      dev_priv->mm.gtt_mappable_end - obj->gtt_offset);
	}
	list_del_init(&obj->gtt_list);
}

/**
 * Update the mappable working set counters. Call _only_ when there is a change
 * in one of (pin|fault)_mappable and update *_mappable _before_ calling.
 * @mappable: new state the changed mappable flag (either pin_ or fault_).
 */
static void
i915_gem_info_update_mappable(struct drm_i915_private *dev_priv,
			      struct drm_i915_gem_object *obj,
			      bool mappable)
{
	if (mappable) {
		if (obj->pin_mappable && obj->fault_mappable)
			/* Combined state was already mappable. */
			return;
		dev_priv->mm.gtt_mappable_count++;
		dev_priv->mm.gtt_mappable_memory += obj->gtt_space->size;
	} else {
		if (obj->pin_mappable || obj->fault_mappable)
			/* Combined state still mappable. */
			return;
		dev_priv->mm.gtt_mappable_count--;
		dev_priv->mm.gtt_mappable_memory -= obj->gtt_space->size;
	}
}

static void i915_gem_info_add_pin(struct drm_i915_private *dev_priv,
				  struct drm_i915_gem_object *obj,
				  bool mappable)
{
	dev_priv->mm.pin_count++;
	dev_priv->mm.pin_memory += obj->gtt_space->size;
	if (mappable) {
		obj->pin_mappable = true;
		i915_gem_info_update_mappable(dev_priv, obj, true);
	}
}

static void i915_gem_info_remove_pin(struct drm_i915_private *dev_priv,
				     struct drm_i915_gem_object *obj)
{
	dev_priv->mm.pin_count--;
	dev_priv->mm.pin_memory -= obj->gtt_space->size;
	if (obj->pin_mappable) {
		obj->pin_mappable = false;
		i915_gem_info_update_mappable(dev_priv, obj, false);
	}
}

int
i915_gem_check_is_wedged(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct completion *x = &dev_priv->error_completion;
	unsigned long flags;
	int ret;

	if (!atomic_read(&dev_priv->mm.wedged))
		return 0;

	ret = wait_for_completion_interruptible(x);
	if (ret)
		return ret;

	/* Success, we reset the GPU! */
	if (!atomic_read(&dev_priv->mm.wedged))
		return 0;

	/* GPU is hung, bump the completion count to account for
	 * the token we just consumed so that we never hit zero and
	 * end up waiting upon a subsequent completion event that
	 * will never happen.
	 */
	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	spin_unlock_irqrestore(&x->wait.lock, flags);
	return -EIO;
}

static int i915_mutex_lock_interruptible(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_check_is_wedged(dev);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (atomic_read(&dev_priv->mm.wedged)) {
		mutex_unlock(&dev->struct_mutex);
		return -EAGAIN;
	}

	WARN_ON(i915_verify_lists(dev));
	return 0;
}

static inline bool
i915_gem_object_is_inactive(struct drm_i915_gem_object *obj)
{
	return obj->gtt_space && !obj->active && obj->pin_count == 0;
}

int i915_gem_do_init(struct drm_device *dev,
		     unsigned long start,
		     unsigned long mappable_end,
		     unsigned long end)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (start >= end ||
	    (start & (PAGE_SIZE - 1)) != 0 ||
	    (end & (PAGE_SIZE - 1)) != 0) {
		return -EINVAL;
	}

	drm_mm_init(&dev_priv->mm.gtt_space, start,
		    end - start);

	dev_priv->mm.gtt_total = end - start;
	dev_priv->mm.mappable_gtt_total = min(end, mappable_end) - start;
	dev_priv->mm.gtt_mappable_end = mappable_end;

	return 0;
}

int
i915_gem_init_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_init *args = data;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = i915_gem_do_init(dev, args->gtt_start, args->gtt_end, args->gtt_end);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_get_aperture *args = data;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	mutex_lock(&dev->struct_mutex);
	args->aper_size = dev_priv->mm.gtt_total;
	args->aper_available_size = args->aper_size - dev_priv->mm.pin_memory;
	mutex_unlock(&dev->struct_mutex);

	return 0;
}


/**
 * Creates a new mm object and returns a handle to it.
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_gem_create *args = data;
	struct drm_i915_gem_object *obj;
	int ret;
	u32 handle;

	args->size = roundup(args->size, PAGE_SIZE);

	/* Allocate the new object */
	obj = i915_gem_alloc_object(dev, args->size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->base);
		i915_gem_info_remove_obj(dev->dev_private, obj->base.size);
		kfree(obj);
		return ret;
	}

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference(&obj->base);
	trace_i915_gem_object_create(obj);

	args->handle = handle;
	return 0;
}

static int i915_gem_object_needs_bit17_swizzle(struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;

	return dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_9_10_17 &&
		obj->tiling_mode != I915_TILING_NONE;
}

static inline void
slow_shmem_copy(struct page *dst_page,
		int dst_offset,
		struct page *src_page,
		int src_offset,
		int length)
{
	char *dst_vaddr, *src_vaddr;

	dst_vaddr = kmap(dst_page);
	src_vaddr = kmap(src_page);

	memcpy(dst_vaddr + dst_offset, src_vaddr + src_offset, length);

	kunmap(src_page);
	kunmap(dst_page);
}

static inline void
slow_shmem_bit17_copy(struct page *gpu_page,
		      int gpu_offset,
		      struct page *cpu_page,
		      int cpu_offset,
		      int length,
		      int is_read)
{
	char *gpu_vaddr, *cpu_vaddr;

	/* Use the unswizzled path if this page isn't affected. */
	if ((page_to_phys(gpu_page) & (1 << 17)) == 0) {
		if (is_read)
			return slow_shmem_copy(cpu_page, cpu_offset,
					       gpu_page, gpu_offset, length);
		else
			return slow_shmem_copy(gpu_page, gpu_offset,
					       cpu_page, cpu_offset, length);
	}

	gpu_vaddr = kmap(gpu_page);
	cpu_vaddr = kmap(cpu_page);

	/* Copy the data, XORing A6 with A17 (1). The user already knows he's
	 * XORing with the other bits (A9 for Y, A9 and A10 for X)
	 */
	while (length > 0) {
		int cacheline_end = ALIGN(gpu_offset + 1, 64);
		int this_length = min(cacheline_end - gpu_offset, length);
		int swizzled_gpu_offset = gpu_offset ^ 64;

		if (is_read) {
			memcpy(cpu_vaddr + cpu_offset,
			       gpu_vaddr + swizzled_gpu_offset,
			       this_length);
		} else {
			memcpy(gpu_vaddr + swizzled_gpu_offset,
			       cpu_vaddr + cpu_offset,
			       this_length);
		}
		cpu_offset += this_length;
		gpu_offset += this_length;
		length -= this_length;
	}

	kunmap(cpu_page);
	kunmap(gpu_page);
}

/**
 * This is the fast shmem pread path, which attempts to copy_from_user directly
 * from the backing pages of the object to the user's address space.  On a
 * fault, it fails so we can fall back to i915_gem_shmem_pwrite_slow().
 */
static int
i915_gem_shmem_pread_fast(struct drm_device *dev,
			  struct drm_i915_gem_object *obj,
			  struct drm_i915_gem_pread *args,
			  struct drm_file *file)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	ssize_t remain;
	loff_t offset;
	char __user *user_data;
	int page_offset, page_length;

	user_data = (char __user *) (uintptr_t) args->data_ptr;
	remain = args->size;

	offset = args->offset;

	while (remain > 0) {
		struct page *page;
		char *vaddr;
		int ret;

		/* Operation in this page
		 *
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		page_offset = offset & (PAGE_SIZE-1);
		page_length = remain;
		if ((page_offset + remain) > PAGE_SIZE)
			page_length = PAGE_SIZE - page_offset;

		page = read_cache_page_gfp(mapping, offset >> PAGE_SHIFT,
					   GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		vaddr = kmap_atomic(page);
		ret = __copy_to_user_inatomic(user_data,
					      vaddr + page_offset,
					      page_length);
		kunmap_atomic(vaddr);

		mark_page_accessed(page);
		page_cache_release(page);
		if (ret)
			return -EFAULT;

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

	return 0;
}

/**
 * This is the fallback shmem pread path, which allocates temporary storage
 * in kernel space to copy_to_user into outside of the struct_mutex, so we
 * can copy out of the object's backing pages while holding the struct mutex
 * and not take page faults.
 */
static int
i915_gem_shmem_pread_slow(struct drm_device *dev,
			  struct drm_i915_gem_object *obj,
			  struct drm_i915_gem_pread *args,
			  struct drm_file *file)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	struct mm_struct *mm = current->mm;
	struct page **user_pages;
	ssize_t remain;
	loff_t offset, pinned_pages, i;
	loff_t first_data_page, last_data_page, num_pages;
	int shmem_page_offset;
	int data_page_index, data_page_offset;
	int page_length;
	int ret;
	uint64_t data_ptr = args->data_ptr;
	int do_bit17_swizzling;

	remain = args->size;

	/* Pin the user pages containing the data.  We can't fault while
	 * holding the struct mutex, yet we want to hold it while
	 * dereferencing the user data.
	 */
	first_data_page = data_ptr / PAGE_SIZE;
	last_data_page = (data_ptr + args->size - 1) / PAGE_SIZE;
	num_pages = last_data_page - first_data_page + 1;

	user_pages = drm_malloc_ab(num_pages, sizeof(struct page *));
	if (user_pages == NULL)
		return -ENOMEM;

	mutex_unlock(&dev->struct_mutex);
	down_read(&mm->mmap_sem);
	pinned_pages = get_user_pages(current, mm, (uintptr_t)args->data_ptr,
				      num_pages, 1, 0, user_pages, NULL);
	up_read(&mm->mmap_sem);
	mutex_lock(&dev->struct_mutex);
	if (pinned_pages < num_pages) {
		ret = -EFAULT;
		goto out;
	}

	ret = i915_gem_object_set_cpu_read_domain_range(obj,
							args->offset,
							args->size);
	if (ret)
		goto out;

	do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);

	offset = args->offset;

	while (remain > 0) {
		struct page *page;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * data_page_index = page number in get_user_pages return
		 * data_page_offset = offset with data_page_index page.
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset & ~PAGE_MASK;
		data_page_index = data_ptr / PAGE_SIZE - first_data_page;
		data_page_offset = data_ptr & ~PAGE_MASK;

		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;
		if ((data_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - data_page_offset;

		page = read_cache_page_gfp(mapping, offset >> PAGE_SHIFT,
					   GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (do_bit17_swizzling) {
			slow_shmem_bit17_copy(page,
					      shmem_page_offset,
					      user_pages[data_page_index],
					      data_page_offset,
					      page_length,
					      1);
		} else {
			slow_shmem_copy(user_pages[data_page_index],
					data_page_offset,
					page,
					shmem_page_offset,
					page_length);
		}

		mark_page_accessed(page);
		page_cache_release(page);

		remain -= page_length;
		data_ptr += page_length;
		offset += page_length;
	}

out:
	for (i = 0; i < pinned_pages; i++) {
		SetPageDirty(user_pages[i]);
		mark_page_accessed(user_pages[i]);
		page_cache_release(user_pages[i]);
	}
	drm_free_large(user_pages);

	return ret;
}

/**
 * Reads data from the object referenced by handle.
 *
 * On error, the contents of *data are undefined.
 */
int
i915_gem_pread_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_pread *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE,
		       (char __user *)(uintptr_t)args->data_ptr,
		       args->size))
		return -EFAULT;

	ret = fault_in_pages_writeable((char __user *)(uintptr_t)args->data_ptr,
				       args->size);
	if (ret)
		return -EFAULT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Bounds check source.  */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto out;
	}

	ret = i915_gem_object_set_cpu_read_domain_range(obj,
							args->offset,
							args->size);
	if (ret)
		goto out;

	ret = -EFAULT;
	if (!i915_gem_object_needs_bit17_swizzle(obj))
		ret = i915_gem_shmem_pread_fast(dev, obj, args, file);
	if (ret == -EFAULT)
		ret = i915_gem_shmem_pread_slow(dev, obj, args, file);

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/* This is the fast write path which cannot handle
 * page faults in the source data
 */

static inline int
fast_user_write(struct io_mapping *mapping,
		loff_t page_base, int page_offset,
		char __user *user_data,
		int length)
{
	char *vaddr_atomic;
	unsigned long unwritten;

	vaddr_atomic = io_mapping_map_atomic_wc(mapping, page_base);
	unwritten = __copy_from_user_inatomic_nocache(vaddr_atomic + page_offset,
						      user_data, length);
	io_mapping_unmap_atomic(vaddr_atomic);
	return unwritten;
}

/* Here's the write path which can sleep for
 * page faults
 */

static inline void
slow_kernel_write(struct io_mapping *mapping,
		  loff_t gtt_base, int gtt_offset,
		  struct page *user_page, int user_offset,
		  int length)
{
	char __iomem *dst_vaddr;
	char *src_vaddr;

	dst_vaddr = io_mapping_map_wc(mapping, gtt_base);
	src_vaddr = kmap(user_page);

	memcpy_toio(dst_vaddr + gtt_offset,
		    src_vaddr + user_offset,
		    length);

	kunmap(user_page);
	io_mapping_unmap(dst_vaddr);
}

/**
 * This is the fast pwrite path, where we copy the data directly from the
 * user into the GTT, uncached.
 */
static int
i915_gem_gtt_pwrite_fast(struct drm_device *dev,
			 struct drm_i915_gem_object *obj,
			 struct drm_i915_gem_pwrite *args,
			 struct drm_file *file)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	ssize_t remain;
	loff_t offset, page_base;
	char __user *user_data;
	int page_offset, page_length;

	user_data = (char __user *) (uintptr_t) args->data_ptr;
	remain = args->size;

	offset = obj->gtt_offset + args->offset;

	while (remain > 0) {
		/* Operation in this page
		 *
		 * page_base = page offset within aperture
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		page_base = (offset & ~(PAGE_SIZE-1));
		page_offset = offset & (PAGE_SIZE-1);
		page_length = remain;
		if ((page_offset + remain) > PAGE_SIZE)
			page_length = PAGE_SIZE - page_offset;

		/* If we get a fault while copying data, then (presumably) our
		 * source page isn't available.  Return the error and we'll
		 * retry in the slow path.
		 */
		if (fast_user_write(dev_priv->mm.gtt_mapping, page_base,
				    page_offset, user_data, page_length))

			return -EFAULT;

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

	return 0;
}

/**
 * This is the fallback GTT pwrite path, which uses get_user_pages to pin
 * the memory and maps it using kmap_atomic for copying.
 *
 * This code resulted in x11perf -rgb10text consuming about 10% more CPU
 * than using i915_gem_gtt_pwrite_fast on a G45 (32-bit).
 */
static int
i915_gem_gtt_pwrite_slow(struct drm_device *dev,
			 struct drm_i915_gem_object *obj,
			 struct drm_i915_gem_pwrite *args,
			 struct drm_file *file)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	ssize_t remain;
	loff_t gtt_page_base, offset;
	loff_t first_data_page, last_data_page, num_pages;
	loff_t pinned_pages, i;
	struct page **user_pages;
	struct mm_struct *mm = current->mm;
	int gtt_page_offset, data_page_offset, data_page_index, page_length;
	int ret;
	uint64_t data_ptr = args->data_ptr;

	remain = args->size;

	/* Pin the user pages containing the data.  We can't fault while
	 * holding the struct mutex, and all of the pwrite implementations
	 * want to hold it while dereferencing the user data.
	 */
	first_data_page = data_ptr / PAGE_SIZE;
	last_data_page = (data_ptr + args->size - 1) / PAGE_SIZE;
	num_pages = last_data_page - first_data_page + 1;

	user_pages = drm_malloc_ab(num_pages, sizeof(struct page *));
	if (user_pages == NULL)
		return -ENOMEM;

	mutex_unlock(&dev->struct_mutex);
	down_read(&mm->mmap_sem);
	pinned_pages = get_user_pages(current, mm, (uintptr_t)args->data_ptr,
				      num_pages, 0, 0, user_pages, NULL);
	up_read(&mm->mmap_sem);
	mutex_lock(&dev->struct_mutex);
	if (pinned_pages < num_pages) {
		ret = -EFAULT;
		goto out_unpin_pages;
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, 1);
	if (ret)
		goto out_unpin_pages;

	offset = obj->gtt_offset + args->offset;

	while (remain > 0) {
		/* Operation in this page
		 *
		 * gtt_page_base = page offset within aperture
		 * gtt_page_offset = offset within page in aperture
		 * data_page_index = page number in get_user_pages return
		 * data_page_offset = offset with data_page_index page.
		 * page_length = bytes to copy for this page
		 */
		gtt_page_base = offset & PAGE_MASK;
		gtt_page_offset = offset & ~PAGE_MASK;
		data_page_index = data_ptr / PAGE_SIZE - first_data_page;
		data_page_offset = data_ptr & ~PAGE_MASK;

		page_length = remain;
		if ((gtt_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - gtt_page_offset;
		if ((data_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - data_page_offset;

		slow_kernel_write(dev_priv->mm.gtt_mapping,
				  gtt_page_base, gtt_page_offset,
				  user_pages[data_page_index],
				  data_page_offset,
				  page_length);

		remain -= page_length;
		offset += page_length;
		data_ptr += page_length;
	}

out_unpin_pages:
	for (i = 0; i < pinned_pages; i++)
		page_cache_release(user_pages[i]);
	drm_free_large(user_pages);

	return ret;
}

/**
 * This is the fast shmem pwrite path, which attempts to directly
 * copy_from_user into the kmapped pages backing the object.
 */
static int
i915_gem_shmem_pwrite_fast(struct drm_device *dev,
			   struct drm_i915_gem_object *obj,
			   struct drm_i915_gem_pwrite *args,
			   struct drm_file *file)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	ssize_t remain;
	loff_t offset;
	char __user *user_data;
	int page_offset, page_length;

	user_data = (char __user *) (uintptr_t) args->data_ptr;
	remain = args->size;

	offset = args->offset;
	obj->dirty = 1;

	while (remain > 0) {
		struct page *page;
		char *vaddr;
		int ret;

		/* Operation in this page
		 *
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		page_offset = offset & (PAGE_SIZE-1);
		page_length = remain;
		if ((page_offset + remain) > PAGE_SIZE)
			page_length = PAGE_SIZE - page_offset;

		page = read_cache_page_gfp(mapping, offset >> PAGE_SHIFT,
					   GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		vaddr = kmap_atomic(page, KM_USER0);
		ret = __copy_from_user_inatomic(vaddr + page_offset,
						user_data,
						page_length);
		kunmap_atomic(vaddr, KM_USER0);

		set_page_dirty(page);
		mark_page_accessed(page);
		page_cache_release(page);

		/* If we get a fault while copying data, then (presumably) our
		 * source page isn't available.  Return the error and we'll
		 * retry in the slow path.
		 */
		if (ret)
			return -EFAULT;

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

	return 0;
}

/**
 * This is the fallback shmem pwrite path, which uses get_user_pages to pin
 * the memory and maps it using kmap_atomic for copying.
 *
 * This avoids taking mmap_sem for faulting on the user's address while the
 * struct_mutex is held.
 */
static int
i915_gem_shmem_pwrite_slow(struct drm_device *dev,
			   struct drm_i915_gem_object *obj,
			   struct drm_i915_gem_pwrite *args,
			   struct drm_file *file)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	struct mm_struct *mm = current->mm;
	struct page **user_pages;
	ssize_t remain;
	loff_t offset, pinned_pages, i;
	loff_t first_data_page, last_data_page, num_pages;
	int shmem_page_offset;
	int data_page_index,  data_page_offset;
	int page_length;
	int ret;
	uint64_t data_ptr = args->data_ptr;
	int do_bit17_swizzling;

	remain = args->size;

	/* Pin the user pages containing the data.  We can't fault while
	 * holding the struct mutex, and all of the pwrite implementations
	 * want to hold it while dereferencing the user data.
	 */
	first_data_page = data_ptr / PAGE_SIZE;
	last_data_page = (data_ptr + args->size - 1) / PAGE_SIZE;
	num_pages = last_data_page - first_data_page + 1;

	user_pages = drm_malloc_ab(num_pages, sizeof(struct page *));
	if (user_pages == NULL)
		return -ENOMEM;

	mutex_unlock(&dev->struct_mutex);
	down_read(&mm->mmap_sem);
	pinned_pages = get_user_pages(current, mm, (uintptr_t)args->data_ptr,
				      num_pages, 0, 0, user_pages, NULL);
	up_read(&mm->mmap_sem);
	mutex_lock(&dev->struct_mutex);
	if (pinned_pages < num_pages) {
		ret = -EFAULT;
		goto out;
	}

	ret = i915_gem_object_set_to_cpu_domain(obj, 1);
	if (ret)
		goto out;

	do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);

	offset = args->offset;
	obj->dirty = 1;

	while (remain > 0) {
		struct page *page;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * data_page_index = page number in get_user_pages return
		 * data_page_offset = offset with data_page_index page.
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset & ~PAGE_MASK;
		data_page_index = data_ptr / PAGE_SIZE - first_data_page;
		data_page_offset = data_ptr & ~PAGE_MASK;

		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;
		if ((data_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - data_page_offset;

		page = read_cache_page_gfp(mapping, offset >> PAGE_SHIFT,
					   GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}

		if (do_bit17_swizzling) {
			slow_shmem_bit17_copy(page,
					      shmem_page_offset,
					      user_pages[data_page_index],
					      data_page_offset,
					      page_length,
					      0);
		} else {
			slow_shmem_copy(page,
					shmem_page_offset,
					user_pages[data_page_index],
					data_page_offset,
					page_length);
		}

		set_page_dirty(page);
		mark_page_accessed(page);
		page_cache_release(page);

		remain -= page_length;
		data_ptr += page_length;
		offset += page_length;
	}

out:
	for (i = 0; i < pinned_pages; i++)
		page_cache_release(user_pages[i]);
	drm_free_large(user_pages);

	return ret;
}

/**
 * Writes data to the object referenced by handle.
 *
 * On error, the contents of the buffer that were to be modified are undefined.
 */
int
i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_gem_pwrite *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_READ,
		       (char __user *)(uintptr_t)args->data_ptr,
		       args->size))
		return -EFAULT;

	ret = fault_in_pages_readable((char __user *)(uintptr_t)args->data_ptr,
				      args->size);
	if (ret)
		return -EFAULT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Bounds check destination. */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto out;
	}

	/* We can only do the GTT pwrite on untiled buffers, as otherwise
	 * it would end up going through the fenced access, and we'll get
	 * different detiling behavior between reading and writing.
	 * pread/pwrite currently are reading and writing from the CPU
	 * perspective, requiring manual detiling by the client.
	 */
	if (obj->phys_obj)
		ret = i915_gem_phys_pwrite(dev, obj, args, file);
	else if (obj->tiling_mode == I915_TILING_NONE &&
		 obj->gtt_space &&
		 obj->base.write_domain != I915_GEM_DOMAIN_CPU) {
		ret = i915_gem_object_pin(obj, 0, true);
		if (ret)
			goto out;

		ret = i915_gem_object_set_to_gtt_domain(obj, 1);
		if (ret)
			goto out_unpin;

		ret = i915_gem_gtt_pwrite_fast(dev, obj, args, file);
		if (ret == -EFAULT)
			ret = i915_gem_gtt_pwrite_slow(dev, obj, args, file);

out_unpin:
		i915_gem_object_unpin(obj);
	} else {
		ret = i915_gem_object_set_to_cpu_domain(obj, 1);
		if (ret)
			goto out;

		ret = -EFAULT;
		if (!i915_gem_object_needs_bit17_swizzle(obj))
			ret = i915_gem_shmem_pwrite_fast(dev, obj, args, file);
		if (ret == -EFAULT)
			ret = i915_gem_shmem_pwrite_slow(dev, obj, args, file);
	}

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when user space prepares to use an object with the CPU, either
 * through the mmap ioctl's mapping or a GTT mapping.
 */
int
i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_set_domain *args = data;
	struct drm_i915_gem_object *obj;
	uint32_t read_domains = args->read_domains;
	uint32_t write_domain = args->write_domain;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	/* Only handle setting domains to types used by the CPU. */
	if (write_domain & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	if (read_domains & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	/* Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain != 0 && read_domains != write_domain)
		return -EINVAL;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	intel_mark_busy(dev, obj);

	if (read_domains & I915_GEM_DOMAIN_GTT) {
		ret = i915_gem_object_set_to_gtt_domain(obj, write_domain != 0);

		/* Update the LRU on the fence for the CPU access that's
		 * about to occur.
		 */
		if (obj->fence_reg != I915_FENCE_REG_NONE) {
			struct drm_i915_fence_reg *reg =
				&dev_priv->fence_regs[obj->fence_reg];
			list_move_tail(&reg->lru_list,
				       &dev_priv->mm.fence_list);
		}

		/* Silently promote "you're not bound, there was nothing to do"
		 * to success, since the client was just asking us to
		 * make sure everything was done.
		 */
		if (ret == -EINVAL)
			ret = 0;
	} else {
		ret = i915_gem_object_set_to_cpu_domain(obj, write_domain != 0);
	}

	/* Maintain LRU order of "inactive" objects */
	if (ret == 0 && i915_gem_object_is_inactive(obj))
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when user space has done writes to this buffer
 */
int
i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file)
{
	struct drm_i915_gem_sw_finish *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Pinned buffers may be scanout, so flush the cache */
	if (obj->pin_count)
		i915_gem_object_flush_cpu_write_domain(obj);

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Maps the contents of an object, returning the address it is mapped
 * into.
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 */
int
i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_mmap *args = data;
	struct drm_gem_object *obj;
	loff_t offset;
	unsigned long addr;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (obj == NULL)
		return -ENOENT;

	if (obj->size > dev_priv->mm.gtt_mappable_end) {
		drm_gem_object_unreference_unlocked(obj);
		return -E2BIG;
	}

	offset = args->offset;

	down_write(&current->mm->mmap_sem);
	addr = do_mmap(obj->filp, 0, args->size,
		       PROT_READ | PROT_WRITE, MAP_SHARED,
		       args->offset);
	up_write(&current->mm->mmap_sem);
	drm_gem_object_unreference_unlocked(obj);
	if (IS_ERR((void *)addr))
		return addr;

	args->addr_ptr = (uint64_t) addr;

	return 0;
}

/**
 * i915_gem_fault - fault a page into the GTT
 * vma: VMA in question
 * vmf: fault info
 *
 * The fault handler is set up by drm_gem_mmap() when a object is GTT mapped
 * from userspace.  The fault handler takes care of binding the object to
 * the GTT (if needed), allocating and programming a fence register (again,
 * only if needed based on whether the old reg is still valid or the object
 * is tiled) and inserting a new PTE into the faulting process.
 *
 * Note that the faulting process may involve evicting existing objects
 * from the GTT and/or fence registers to make room.  So performance may
 * suffer if the GTT working set is large or there are few fence registers
 * left.
 */
int i915_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_i915_gem_object *obj = to_intel_bo(vma->vm_private_data);
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	pgoff_t page_offset;
	unsigned long pfn;
	int ret = 0;
	bool write = !!(vmf->flags & FAULT_FLAG_WRITE);

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;

	/* Now bind it into the GTT if needed */
	mutex_lock(&dev->struct_mutex);
	BUG_ON(obj->pin_count && !obj->pin_mappable);

	if (!obj->map_and_fenceable) {
		ret = i915_gem_object_unbind(obj);
		if (ret)
			goto unlock;
	}

	if (!obj->gtt_space) {
		ret = i915_gem_object_bind_to_gtt(obj, 0, true);
		if (ret)
			goto unlock;
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, write);
	if (ret)
		goto unlock;

	if (!obj->fault_mappable) {
		obj->fault_mappable = true;
		i915_gem_info_update_mappable(dev_priv, obj, true);
	}

	/* Need a new fence register? */
	if (obj->tiling_mode != I915_TILING_NONE) {
		ret = i915_gem_object_get_fence_reg(obj, true);
		if (ret)
			goto unlock;
	}

	if (i915_gem_object_is_inactive(obj))
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	pfn = ((dev->agp->base + obj->gtt_offset) >> PAGE_SHIFT) +
		page_offset;

	/* Finally, remap it using the new GTT offset */
	ret = vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);
unlock:
	mutex_unlock(&dev->struct_mutex);

	switch (ret) {
	case -EAGAIN:
		set_need_resched();
	case 0:
	case -ERESTARTSYS:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

/**
 * i915_gem_create_mmap_offset - create a fake mmap offset for an object
 * @obj: obj in question
 *
 * GEM memory mapping works by handing back to userspace a fake mmap offset
 * it can use in a subsequent mmap(2) call.  The DRM core code then looks
 * up the object based on the offset and sets up the various memory mapping
 * structures.
 *
 * This routine allocates and attaches a fake offset for @obj.
 */
static int
i915_gem_create_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list;
	struct drm_local_map *map;
	int ret = 0;

	/* Set the object up for mmap'ing */
	list = &obj->base.map_list;
	list->map = kzalloc(sizeof(struct drm_map_list), GFP_KERNEL);
	if (!list->map)
		return -ENOMEM;

	map = list->map;
	map->type = _DRM_GEM;
	map->size = obj->base.size;
	map->handle = obj;

	/* Get a DRM GEM mmap offset allocated... */
	list->file_offset_node = drm_mm_search_free(&mm->offset_manager,
						    obj->base.size / PAGE_SIZE,
						    0, 0);
	if (!list->file_offset_node) {
		DRM_ERROR("failed to allocate offset for bo %d\n",
			  obj->base.name);
		ret = -ENOSPC;
		goto out_free_list;
	}

	list->file_offset_node = drm_mm_get_block(list->file_offset_node,
						  obj->base.size / PAGE_SIZE,
						  0);
	if (!list->file_offset_node) {
		ret = -ENOMEM;
		goto out_free_list;
	}

	list->hash.key = list->file_offset_node->start;
	ret = drm_ht_insert_item(&mm->offset_hash, &list->hash);
	if (ret) {
		DRM_ERROR("failed to add to map hash\n");
		goto out_free_mm;
	}

	return 0;

out_free_mm:
	drm_mm_put_block(list->file_offset_node);
out_free_list:
	kfree(list->map);
	list->map = NULL;

	return ret;
}

/**
 * i915_gem_release_mmap - remove physical page mappings
 * @obj: obj in question
 *
 * Preserve the reservation of the mmapping with the DRM core code, but
 * relinquish ownership of the pages back to the system.
 *
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by i915_gem_fault().
 */
void
i915_gem_release_mmap(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (unlikely(obj->base.map_list.map && dev->dev_mapping))
		unmap_mapping_range(dev->dev_mapping,
				    (loff_t)obj->base.map_list.hash.key<<PAGE_SHIFT,
				    obj->base.size, 1);

	if (obj->fault_mappable) {
		obj->fault_mappable = false;
		i915_gem_info_update_mappable(dev_priv, obj, false);
	}
}

static void
i915_gem_free_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list = &obj->base.map_list;

	drm_ht_remove_item(&mm->offset_hash, &list->hash);
	drm_mm_put_block(list->file_offset_node);
	kfree(list->map);
	list->map = NULL;
}

static uint32_t
i915_gem_get_gtt_size(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	uint32_t size;

	if (INTEL_INFO(dev)->gen >= 4 ||
	    obj->tiling_mode == I915_TILING_NONE)
		return obj->base.size;

	/* Previous chips need a power-of-two fence region when tiling */
	if (INTEL_INFO(dev)->gen == 3)
		size = 1024*1024;
	else
		size = 512*1024;

	while (size < obj->base.size)
		size <<= 1;

	return size;
}

/**
 * i915_gem_get_gtt_alignment - return required GTT alignment for an object
 * @obj: object to check
 *
 * Return the required GTT alignment for an object, taking into account
 * potential fence register mapping.
 */
static uint32_t
i915_gem_get_gtt_alignment(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;

	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (INTEL_INFO(dev)->gen >= 4 ||
	    obj->tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	return i915_gem_get_gtt_size(obj);
}

/**
 * i915_gem_get_unfenced_gtt_alignment - return required GTT alignment for an
 *					 unfenced object
 * @obj: object to check
 *
 * Return the required GTT alignment for an object, only taking into account
 * unfenced tiled surface requirements.
 */
static uint32_t
i915_gem_get_unfenced_gtt_alignment(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	int tile_height;

	/*
	 * Minimum alignment is 4k (GTT page size) for sane hw.
	 */
	if (INTEL_INFO(dev)->gen >= 4 || IS_G33(dev) ||
	    obj->tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Older chips need unfenced tiled buffers to be aligned to the left
	 * edge of an even tile row (where tile rows are counted as if the bo is
	 * placed in a fenced gtt region).
	 */
	if (IS_GEN2(dev) ||
	    (obj->tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev)))
		tile_height = 32;
	else
		tile_height = 8;

	return tile_height * obj->stride * 2;
}

/**
 * i915_gem_mmap_gtt_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_mmap_gtt *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->base.size > dev_priv->mm.gtt_mappable_end) {
		ret = -E2BIG;
		goto unlock;
	}

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to mmap a purgeable buffer\n");
		ret = -EINVAL;
		goto out;
	}

	if (!obj->base.map_list.map) {
		ret = i915_gem_create_mmap_offset(obj);
		if (ret)
			goto out;
	}

	args->offset = (u64)obj->base.map_list.hash.key << PAGE_SHIFT;

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int
i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj,
			      gfp_t gfpmask)
{
	int page_count, i;
	struct address_space *mapping;
	struct inode *inode;
	struct page *page;

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 */
	page_count = obj->base.size / PAGE_SIZE;
	BUG_ON(obj->pages != NULL);
	obj->pages = drm_malloc_ab(page_count, sizeof(struct page *));
	if (obj->pages == NULL)
		return -ENOMEM;

	inode = obj->base.filp->f_path.dentry->d_inode;
	mapping = inode->i_mapping;
	for (i = 0; i < page_count; i++) {
		page = read_cache_page_gfp(mapping, i,
					   GFP_HIGHUSER |
					   __GFP_COLD |
					   __GFP_RECLAIMABLE |
					   gfpmask);
		if (IS_ERR(page))
			goto err_pages;

		obj->pages[i] = page;
	}

	if (obj->tiling_mode != I915_TILING_NONE)
		i915_gem_object_do_bit_17_swizzle(obj);

	return 0;

err_pages:
	while (i--)
		page_cache_release(obj->pages[i]);

	drm_free_large(obj->pages);
	obj->pages = NULL;
	return PTR_ERR(page);
}

static void
i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj)
{
	int page_count = obj->base.size / PAGE_SIZE;
	int i;

	BUG_ON(obj->madv == __I915_MADV_PURGED);

	if (obj->tiling_mode != I915_TILING_NONE)
		i915_gem_object_save_bit_17_swizzle(obj);

	if (obj->madv == I915_MADV_DONTNEED)
		obj->dirty = 0;

	for (i = 0; i < page_count; i++) {
		if (obj->dirty)
			set_page_dirty(obj->pages[i]);

		if (obj->madv == I915_MADV_WILLNEED)
			mark_page_accessed(obj->pages[i]);

		page_cache_release(obj->pages[i]);
	}
	obj->dirty = 0;

	drm_free_large(obj->pages);
	obj->pages = NULL;
}

static uint32_t
i915_gem_next_request_seqno(struct drm_device *dev,
			    struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return ring->outstanding_lazy_request = dev_priv->next_seqno;
}

static void
i915_gem_object_move_to_active(struct drm_i915_gem_object *obj,
			       struct intel_ring_buffer *ring)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t seqno = i915_gem_next_request_seqno(dev, ring);

	BUG_ON(ring == NULL);
	obj->ring = ring;

	/* Add a reference if we're newly entering the active list. */
	if (!obj->active) {
		drm_gem_object_reference(&obj->base);
		obj->active = 1;
	}

	/* Move from whatever list we were on to the tail of execution. */
	list_move_tail(&obj->mm_list, &dev_priv->mm.active_list);
	list_move_tail(&obj->ring_list, &ring->active_list);

	obj->last_rendering_seqno = seqno;
	if (obj->fenced_gpu_access) {
		struct drm_i915_fence_reg *reg;

		BUG_ON(obj->fence_reg == I915_FENCE_REG_NONE);

		obj->last_fenced_seqno = seqno;
		obj->last_fenced_ring = ring;

		reg = &dev_priv->fence_regs[obj->fence_reg];
		list_move_tail(&reg->lru_list, &dev_priv->mm.fence_list);
	}
}

static void
i915_gem_object_move_off_active(struct drm_i915_gem_object *obj)
{
	list_del_init(&obj->ring_list);
	obj->last_rendering_seqno = 0;
	obj->last_fenced_seqno = 0;
}

static void
i915_gem_object_move_to_flushing(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	BUG_ON(!obj->active);
	list_move_tail(&obj->mm_list, &dev_priv->mm.flushing_list);

	i915_gem_object_move_off_active(obj);
}

static void
i915_gem_object_move_to_inactive(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (obj->pin_count != 0)
		list_move_tail(&obj->mm_list, &dev_priv->mm.pinned_list);
	else
		list_move_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	BUG_ON(!list_empty(&obj->gpu_write_list));
	BUG_ON(!obj->active);
	obj->ring = NULL;

	i915_gem_object_move_off_active(obj);
	obj->fenced_gpu_access = false;
	obj->last_fenced_ring = NULL;

	obj->active = 0;
	drm_gem_object_unreference(&obj->base);

	WARN_ON(i915_verify_lists(dev));
}

/* Immediately discard the backing storage */
static void
i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	struct inode *inode;

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*. Here we mirror the actions taken
	 * when by shmem_delete_inode() to release the backing store.
	 */
	inode = obj->base.filp->f_path.dentry->d_inode;
	truncate_inode_pages(inode->i_mapping, 0);
	if (inode->i_op->truncate_range)
		inode->i_op->truncate_range(inode, 0, (loff_t)-1);

	obj->madv = __I915_MADV_PURGED;
}

static inline int
i915_gem_object_is_purgeable(struct drm_i915_gem_object *obj)
{
	return obj->madv == I915_MADV_DONTNEED;
}

static void
i915_gem_process_flushing_list(struct drm_device *dev,
			       uint32_t flush_domains,
			       struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_object *obj, *next;

	list_for_each_entry_safe(obj, next,
				 &ring->gpu_write_list,
				 gpu_write_list) {
		if (obj->base.write_domain & flush_domains) {
			uint32_t old_write_domain = obj->base.write_domain;

			obj->base.write_domain = 0;
			list_del_init(&obj->gpu_write_list);
			i915_gem_object_move_to_active(obj, ring);

			trace_i915_gem_object_change_domain(obj,
							    obj->base.read_domains,
							    old_write_domain);
		}
	}
}

int
i915_add_request(struct drm_device *dev,
		 struct drm_file *file,
		 struct drm_i915_gem_request *request,
		 struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_file_private *file_priv = NULL;
	uint32_t seqno;
	int was_empty;
	int ret;

	BUG_ON(request == NULL);

	if (file != NULL)
		file_priv = file->driver_priv;

	ret = ring->add_request(ring, &seqno);
	if (ret)
	    return ret;

	ring->outstanding_lazy_request = false;

	request->seqno = seqno;
	request->ring = ring;
	request->emitted_jiffies = jiffies;
	was_empty = list_empty(&ring->request_list);
	list_add_tail(&request->list, &ring->request_list);

	if (file_priv) {
		spin_lock(&file_priv->mm.lock);
		request->file_priv = file_priv;
		list_add_tail(&request->client_list,
			      &file_priv->mm.request_list);
		spin_unlock(&file_priv->mm.lock);
	}

	if (!dev_priv->mm.suspended) {
		mod_timer(&dev_priv->hangcheck_timer,
			  jiffies + msecs_to_jiffies(DRM_I915_HANGCHECK_PERIOD));
		if (was_empty)
			queue_delayed_work(dev_priv->wq,
					   &dev_priv->mm.retire_work, HZ);
	}
	return 0;
}

/**
 * Command execution barrier
 *
 * Ensures that all commands in the ring are finished
 * before signalling the CPU
 */
static void
i915_retire_commands(struct drm_device *dev, struct intel_ring_buffer *ring)
{
	uint32_t flush_domains = 0;

	/* The sampler always gets flushed on i965 (sigh) */
	if (INTEL_INFO(dev)->gen >= 4)
		flush_domains |= I915_GEM_DOMAIN_SAMPLER;

	ring->flush(ring, I915_GEM_DOMAIN_COMMAND, flush_domains);
}

static inline void
i915_gem_request_remove_from_client(struct drm_i915_gem_request *request)
{
	struct drm_i915_file_private *file_priv = request->file_priv;

	if (!file_priv)
		return;

	spin_lock(&file_priv->mm.lock);
	list_del(&request->client_list);
	request->file_priv = NULL;
	spin_unlock(&file_priv->mm.lock);
}

static void i915_gem_reset_ring_lists(struct drm_i915_private *dev_priv,
				      struct intel_ring_buffer *ring)
{
	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		list_del(&request->list);
		i915_gem_request_remove_from_client(request);
		kfree(request);
	}

	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&ring->active_list,
				       struct drm_i915_gem_object,
				       ring_list);

		obj->base.write_domain = 0;
		list_del_init(&obj->gpu_write_list);
		i915_gem_object_move_to_inactive(obj);
	}
}

void i915_gem_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int i;

	i915_gem_reset_ring_lists(dev_priv, &dev_priv->render_ring);
	i915_gem_reset_ring_lists(dev_priv, &dev_priv->bsd_ring);
	i915_gem_reset_ring_lists(dev_priv, &dev_priv->blt_ring);

	/* Remove anything from the flushing lists. The GPU cache is likely
	 * to be lost on reset along with the data, so simply move the
	 * lost bo to the inactive list.
	 */
	while (!list_empty(&dev_priv->mm.flushing_list)) {
		obj= list_first_entry(&dev_priv->mm.flushing_list,
				      struct drm_i915_gem_object,
				      mm_list);

		obj->base.write_domain = 0;
		list_del_init(&obj->gpu_write_list);
		i915_gem_object_move_to_inactive(obj);
	}

	/* Move everything out of the GPU domains to ensure we do any
	 * necessary invalidation upon reuse.
	 */
	list_for_each_entry(obj,
			    &dev_priv->mm.inactive_list,
			    mm_list)
	{
		obj->base.read_domains &= ~I915_GEM_GPU_DOMAINS;
	}

	/* The fence registers are invalidated so clear them out */
	for (i = 0; i < 16; i++) {
		struct drm_i915_fence_reg *reg;

		reg = &dev_priv->fence_regs[i];
		if (!reg->obj)
			continue;

		i915_gem_clear_fence_reg(reg->obj);
	}
}

/**
 * This function clears the request list as sequence numbers are passed.
 */
static void
i915_gem_retire_requests_ring(struct drm_device *dev,
			      struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t seqno;

	if (!ring->status_page.page_addr ||
	    list_empty(&ring->request_list))
		return;

	WARN_ON(i915_verify_lists(dev));

	seqno = ring->get_seqno(ring);
	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		if (!i915_seqno_passed(seqno, request->seqno))
			break;

		trace_i915_gem_request_retire(dev, request->seqno);

		list_del(&request->list);
		i915_gem_request_remove_from_client(request);
		kfree(request);
	}

	/* Move any buffers on the active list that are no longer referenced
	 * by the ringbuffer to the flushing/inactive lists as appropriate.
	 */
	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj= list_first_entry(&ring->active_list,
				      struct drm_i915_gem_object,
				      ring_list);

		if (!i915_seqno_passed(seqno, obj->last_rendering_seqno))
			break;

		if (obj->base.write_domain != 0)
			i915_gem_object_move_to_flushing(obj);
		else
			i915_gem_object_move_to_inactive(obj);
	}

	if (unlikely (dev_priv->trace_irq_seqno &&
		      i915_seqno_passed(dev_priv->trace_irq_seqno, seqno))) {
		ring->user_irq_put(ring);
		dev_priv->trace_irq_seqno = 0;
	}

	WARN_ON(i915_verify_lists(dev));
}

void
i915_gem_retire_requests(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!list_empty(&dev_priv->mm.deferred_free_list)) {
	    struct drm_i915_gem_object *obj, *next;

	    /* We must be careful that during unbind() we do not
	     * accidentally infinitely recurse into retire requests.
	     * Currently:
	     *   retire -> free -> unbind -> wait -> retire_ring
	     */
	    list_for_each_entry_safe(obj, next,
				     &dev_priv->mm.deferred_free_list,
				     mm_list)
		    i915_gem_free_object_tail(obj);
	}

	i915_gem_retire_requests_ring(dev, &dev_priv->render_ring);
	i915_gem_retire_requests_ring(dev, &dev_priv->bsd_ring);
	i915_gem_retire_requests_ring(dev, &dev_priv->blt_ring);
}

static void
i915_gem_retire_work_handler(struct work_struct *work)
{
	drm_i915_private_t *dev_priv;
	struct drm_device *dev;

	dev_priv = container_of(work, drm_i915_private_t,
				mm.retire_work.work);
	dev = dev_priv->dev;

	/* Come back later if the device is busy... */
	if (!mutex_trylock(&dev->struct_mutex)) {
		queue_delayed_work(dev_priv->wq, &dev_priv->mm.retire_work, HZ);
		return;
	}

	i915_gem_retire_requests(dev);

	if (!dev_priv->mm.suspended &&
		(!list_empty(&dev_priv->render_ring.request_list) ||
		 !list_empty(&dev_priv->bsd_ring.request_list) ||
		 !list_empty(&dev_priv->blt_ring.request_list)))
		queue_delayed_work(dev_priv->wq, &dev_priv->mm.retire_work, HZ);
	mutex_unlock(&dev->struct_mutex);
}

int
i915_do_wait_request(struct drm_device *dev, uint32_t seqno,
		     bool interruptible, struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 ier;
	int ret = 0;

	BUG_ON(seqno == 0);

	if (atomic_read(&dev_priv->mm.wedged))
		return -EAGAIN;

	if (seqno == ring->outstanding_lazy_request) {
		struct drm_i915_gem_request *request;

		request = kzalloc(sizeof(*request), GFP_KERNEL);
		if (request == NULL)
			return -ENOMEM;

		ret = i915_add_request(dev, NULL, request, ring);
		if (ret) {
			kfree(request);
			return ret;
		}

		seqno = request->seqno;
	}

	if (!i915_seqno_passed(ring->get_seqno(ring), seqno)) {
		if (HAS_PCH_SPLIT(dev))
			ier = I915_READ(DEIER) | I915_READ(GTIER);
		else
			ier = I915_READ(IER);
		if (!ier) {
			DRM_ERROR("something (likely vbetool) disabled "
				  "interrupts, re-enabling\n");
			i915_driver_irq_preinstall(dev);
			i915_driver_irq_postinstall(dev);
		}

		trace_i915_gem_request_wait_begin(dev, seqno);

		ring->waiting_seqno = seqno;
		ring->user_irq_get(ring);
		if (interruptible)
			ret = wait_event_interruptible(ring->irq_queue,
				i915_seqno_passed(ring->get_seqno(ring), seqno)
				|| atomic_read(&dev_priv->mm.wedged));
		else
			wait_event(ring->irq_queue,
				i915_seqno_passed(ring->get_seqno(ring), seqno)
				|| atomic_read(&dev_priv->mm.wedged));

		ring->user_irq_put(ring);
		ring->waiting_seqno = 0;

		trace_i915_gem_request_wait_end(dev, seqno);
	}
	if (atomic_read(&dev_priv->mm.wedged))
		ret = -EAGAIN;

	if (ret && ret != -ERESTARTSYS)
		DRM_ERROR("%s returns %d (awaiting %d at %d, next %d)\n",
			  __func__, ret, seqno, ring->get_seqno(ring),
			  dev_priv->next_seqno);

	/* Directly dispatch request retiring.  While we have the work queue
	 * to handle this, the waiter on a request often wants an associated
	 * buffer to have made it to the inactive list, and we would need
	 * a separate wait queue to handle that.
	 */
	if (ret == 0)
		i915_gem_retire_requests_ring(dev, ring);

	return ret;
}

/**
 * Waits for a sequence number to be signaled, and cleans up the
 * request and object lists appropriately for that event.
 */
static int
i915_wait_request(struct drm_device *dev, uint32_t seqno,
		  struct intel_ring_buffer *ring)
{
	return i915_do_wait_request(dev, seqno, 1, ring);
}

static void
i915_gem_flush_ring(struct drm_device *dev,
		    struct intel_ring_buffer *ring,
		    uint32_t invalidate_domains,
		    uint32_t flush_domains)
{
	ring->flush(ring, invalidate_domains, flush_domains);
	i915_gem_process_flushing_list(dev, flush_domains, ring);
}

static void
i915_gem_flush(struct drm_device *dev,
	       uint32_t invalidate_domains,
	       uint32_t flush_domains,
	       uint32_t flush_rings)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (flush_domains & I915_GEM_DOMAIN_CPU)
		intel_gtt_chipset_flush();

	if ((flush_domains | invalidate_domains) & I915_GEM_GPU_DOMAINS) {
		if (flush_rings & RING_RENDER)
			i915_gem_flush_ring(dev, &dev_priv->render_ring,
					    invalidate_domains, flush_domains);
		if (flush_rings & RING_BSD)
			i915_gem_flush_ring(dev, &dev_priv->bsd_ring,
					    invalidate_domains, flush_domains);
		if (flush_rings & RING_BLT)
			i915_gem_flush_ring(dev, &dev_priv->blt_ring,
					    invalidate_domains, flush_domains);
	}
}

/**
 * Ensures that all rendering to the object has completed and the object is
 * safe to unbind from the GTT or access from the CPU.
 */
static int
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool interruptible)
{
	struct drm_device *dev = obj->base.dev;
	int ret;

	/* This function only exists to support waiting for existing rendering,
	 * not for emitting required flushes.
	 */
	BUG_ON((obj->base.write_domain & I915_GEM_GPU_DOMAINS) != 0);

	/* If there is rendering queued on the buffer being evicted, wait for
	 * it.
	 */
	if (obj->active) {
		ret = i915_do_wait_request(dev,
					   obj->last_rendering_seqno,
					   interruptible,
					   obj->ring);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * Unbinds an object from the GTT aperture.
 */
int
i915_gem_object_unbind(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = 0;

	if (obj->gtt_space == NULL)
		return 0;

	if (obj->pin_count != 0) {
		DRM_ERROR("Attempting to unbind pinned buffer\n");
		return -EINVAL;
	}

	/* blow away mappings if mapped through GTT */
	i915_gem_release_mmap(obj);

	/* Move the object to the CPU domain to ensure that
	 * any possible CPU writes while it's not in the GTT
	 * are flushed when we go to remap it. This will
	 * also ensure that all pending GPU writes are finished
	 * before we unbind.
	 */
	ret = i915_gem_object_set_to_cpu_domain(obj, 1);
	if (ret == -ERESTARTSYS)
		return ret;
	/* Continue on if we fail due to EIO, the GPU is hung so we
	 * should be safe and we need to cleanup or else we might
	 * cause memory corruption through use-after-free.
	 */
	if (ret) {
		i915_gem_clflush_object(obj);
		obj->base.read_domains = obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	/* release the fence reg _after_ flushing */
	if (obj->fence_reg != I915_FENCE_REG_NONE)
		i915_gem_clear_fence_reg(obj);

	i915_gem_gtt_unbind_object(obj);

	i915_gem_object_put_pages_gtt(obj);

	i915_gem_info_remove_gtt(dev_priv, obj);
	list_del_init(&obj->mm_list);
	/* Avoid an unnecessary call to unbind on rebind. */
	obj->map_and_fenceable = true;

	drm_mm_put_block(obj->gtt_space);
	obj->gtt_space = NULL;
	obj->gtt_offset = 0;

	if (i915_gem_object_is_purgeable(obj))
		i915_gem_object_truncate(obj);

	trace_i915_gem_object_unbind(obj);

	return ret;
}

static int i915_ring_idle(struct drm_device *dev,
			  struct intel_ring_buffer *ring)
{
	if (list_empty(&ring->gpu_write_list) && list_empty(&ring->active_list))
		return 0;

	i915_gem_flush_ring(dev, ring,
			    I915_GEM_GPU_DOMAINS, I915_GEM_GPU_DOMAINS);
	return i915_wait_request(dev,
				 i915_gem_next_request_seqno(dev, ring),
				 ring);
}

int
i915_gpu_idle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	bool lists_empty;
	int ret;

	lists_empty = (list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->mm.active_list));
	if (lists_empty)
		return 0;

	/* Flush everything onto the inactive list. */
	ret = i915_ring_idle(dev, &dev_priv->render_ring);
	if (ret)
		return ret;

	ret = i915_ring_idle(dev, &dev_priv->bsd_ring);
	if (ret)
		return ret;

	ret = i915_ring_idle(dev, &dev_priv->blt_ring);
	if (ret)
		return ret;

	return 0;
}

static int sandybridge_write_fence_reg(struct drm_i915_gem_object *obj,
				       struct intel_ring_buffer *pipelined)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size = obj->gtt_space->size;
	int regnum = obj->fence_reg;
	uint64_t val;

	val = (uint64_t)((obj->gtt_offset + size - 4096) &
			 0xfffff000) << 32;
	val |= obj->gtt_offset & 0xfffff000;
	val |= (uint64_t)((obj->stride / 128) - 1) <<
		SANDYBRIDGE_FENCE_PITCH_SHIFT;

	if (obj->tiling_mode == I915_TILING_Y)
		val |= 1 << I965_FENCE_TILING_Y_SHIFT;
	val |= I965_FENCE_REG_VALID;

	if (pipelined) {
		int ret = intel_ring_begin(pipelined, 6);
		if (ret)
			return ret;

		intel_ring_emit(pipelined, MI_NOOP);
		intel_ring_emit(pipelined, MI_LOAD_REGISTER_IMM(2));
		intel_ring_emit(pipelined, FENCE_REG_SANDYBRIDGE_0 + regnum*8);
		intel_ring_emit(pipelined, (u32)val);
		intel_ring_emit(pipelined, FENCE_REG_SANDYBRIDGE_0 + regnum*8 + 4);
		intel_ring_emit(pipelined, (u32)(val >> 32));
		intel_ring_advance(pipelined);
	} else
		I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + regnum * 8, val);

	return 0;
}

static int i965_write_fence_reg(struct drm_i915_gem_object *obj,
				struct intel_ring_buffer *pipelined)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size = obj->gtt_space->size;
	int regnum = obj->fence_reg;
	uint64_t val;

	val = (uint64_t)((obj->gtt_offset + size - 4096) &
		    0xfffff000) << 32;
	val |= obj->gtt_offset & 0xfffff000;
	val |= ((obj->stride / 128) - 1) << I965_FENCE_PITCH_SHIFT;
	if (obj->tiling_mode == I915_TILING_Y)
		val |= 1 << I965_FENCE_TILING_Y_SHIFT;
	val |= I965_FENCE_REG_VALID;

	if (pipelined) {
		int ret = intel_ring_begin(pipelined, 6);
		if (ret)
			return ret;

		intel_ring_emit(pipelined, MI_NOOP);
		intel_ring_emit(pipelined, MI_LOAD_REGISTER_IMM(2));
		intel_ring_emit(pipelined, FENCE_REG_965_0 + regnum*8);
		intel_ring_emit(pipelined, (u32)val);
		intel_ring_emit(pipelined, FENCE_REG_965_0 + regnum*8 + 4);
		intel_ring_emit(pipelined, (u32)(val >> 32));
		intel_ring_advance(pipelined);
	} else
		I915_WRITE64(FENCE_REG_965_0 + regnum * 8, val);

	return 0;
}

static int i915_write_fence_reg(struct drm_i915_gem_object *obj,
				struct intel_ring_buffer *pipelined)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size = obj->gtt_space->size;
	u32 fence_reg, val, pitch_val;
	int tile_width;

	if (WARN((obj->gtt_offset & ~I915_FENCE_START_MASK) ||
		 (size & -size) != size ||
		 (obj->gtt_offset & (size - 1)),
		 "object 0x%08x [fenceable? %d] not 1M or pot-size (0x%08x) aligned\n",
		 obj->gtt_offset, obj->map_and_fenceable, size))
		return -EINVAL;

	if (obj->tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev))
		tile_width = 128;
	else
		tile_width = 512;

	/* Note: pitch better be a power of two tile widths */
	pitch_val = obj->stride / tile_width;
	pitch_val = ffs(pitch_val) - 1;

	val = obj->gtt_offset;
	if (obj->tiling_mode == I915_TILING_Y)
		val |= 1 << I830_FENCE_TILING_Y_SHIFT;
	val |= I915_FENCE_SIZE_BITS(size);
	val |= pitch_val << I830_FENCE_PITCH_SHIFT;
	val |= I830_FENCE_REG_VALID;

	fence_reg = obj->fence_reg;
	if (fence_reg < 8)
		fence_reg = FENCE_REG_830_0 + fence_reg * 4;
	else
		fence_reg = FENCE_REG_945_8 + (fence_reg - 8) * 4;

	if (pipelined) {
		int ret = intel_ring_begin(pipelined, 4);
		if (ret)
			return ret;

		intel_ring_emit(pipelined, MI_NOOP);
		intel_ring_emit(pipelined, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit(pipelined, fence_reg);
		intel_ring_emit(pipelined, val);
		intel_ring_advance(pipelined);
	} else
		I915_WRITE(fence_reg, val);

	return 0;
}

static int i830_write_fence_reg(struct drm_i915_gem_object *obj,
				struct intel_ring_buffer *pipelined)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size = obj->gtt_space->size;
	int regnum = obj->fence_reg;
	uint32_t val;
	uint32_t pitch_val;

	if (WARN((obj->gtt_offset & ~I830_FENCE_START_MASK) ||
		 (size & -size) != size ||
		 (obj->gtt_offset & (size - 1)),
		 "object 0x%08x not 512K or pot-size 0x%08x aligned\n",
		 obj->gtt_offset, size))
		return -EINVAL;

	pitch_val = obj->stride / 128;
	pitch_val = ffs(pitch_val) - 1;

	val = obj->gtt_offset;
	if (obj->tiling_mode == I915_TILING_Y)
		val |= 1 << I830_FENCE_TILING_Y_SHIFT;
	val |= I830_FENCE_SIZE_BITS(size);
	val |= pitch_val << I830_FENCE_PITCH_SHIFT;
	val |= I830_FENCE_REG_VALID;

	if (pipelined) {
		int ret = intel_ring_begin(pipelined, 4);
		if (ret)
			return ret;

		intel_ring_emit(pipelined, MI_NOOP);
		intel_ring_emit(pipelined, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit(pipelined, FENCE_REG_830_0 + regnum*4);
		intel_ring_emit(pipelined, val);
		intel_ring_advance(pipelined);
	} else
		I915_WRITE(FENCE_REG_830_0 + regnum * 4, val);

	return 0;
}

static int i915_find_fence_reg(struct drm_device *dev,
			       bool interruptible)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_fence_reg *reg;
	struct drm_i915_gem_object *obj = NULL;
	int i, avail, ret;

	/* First try to find a free reg */
	avail = 0;
	for (i = dev_priv->fence_reg_start; i < dev_priv->num_fence_regs; i++) {
		reg = &dev_priv->fence_regs[i];
		if (!reg->obj)
			return i;

		if (!reg->obj->pin_count)
			avail++;
	}

	if (avail == 0)
		return -ENOSPC;

	/* None available, try to steal one or wait for a user to finish */
	avail = I915_FENCE_REG_NONE;
	list_for_each_entry(reg, &dev_priv->mm.fence_list,
			    lru_list) {
		obj = reg->obj;
		if (obj->pin_count)
			continue;

		/* found one! */
		avail = obj->fence_reg;
		break;
	}

	BUG_ON(avail == I915_FENCE_REG_NONE);

	/* We only have a reference on obj from the active list. put_fence_reg
	 * might drop that one, causing a use-after-free in it. So hold a
	 * private reference to obj like the other callers of put_fence_reg
	 * (set_tiling ioctl) do. */
	drm_gem_object_reference(&obj->base);
	ret = i915_gem_object_put_fence_reg(obj, interruptible);
	drm_gem_object_unreference(&obj->base);
	if (ret != 0)
		return ret;

	return avail;
}

/**
 * i915_gem_object_get_fence_reg - set up a fence reg for an object
 * @obj: object to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 *
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 */
int
i915_gem_object_get_fence_reg(struct drm_i915_gem_object *obj,
			      bool interruptible)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_fence_reg *reg = NULL;
	struct intel_ring_buffer *pipelined = NULL;
	int ret;

	/* Just update our place in the LRU if our fence is getting used. */
	if (obj->fence_reg != I915_FENCE_REG_NONE) {
		reg = &dev_priv->fence_regs[obj->fence_reg];
		list_move_tail(&reg->lru_list, &dev_priv->mm.fence_list);
		return 0;
	}

	switch (obj->tiling_mode) {
	case I915_TILING_NONE:
		WARN(1, "allocating a fence for non-tiled object?\n");
		break;
	case I915_TILING_X:
		if (!obj->stride)
			return -EINVAL;
		WARN((obj->stride & (512 - 1)),
		     "object 0x%08x is X tiled but has non-512B pitch\n",
		     obj->gtt_offset);
		break;
	case I915_TILING_Y:
		if (!obj->stride)
			return -EINVAL;
		WARN((obj->stride & (128 - 1)),
		     "object 0x%08x is Y tiled but has non-128B pitch\n",
		     obj->gtt_offset);
		break;
	}

	ret = i915_find_fence_reg(dev, interruptible);
	if (ret < 0)
		return ret;

	obj->fence_reg = ret;
	reg = &dev_priv->fence_regs[obj->fence_reg];
	list_add_tail(&reg->lru_list, &dev_priv->mm.fence_list);

	reg->obj = obj;

	switch (INTEL_INFO(dev)->gen) {
	case 6:
		ret = sandybridge_write_fence_reg(obj, pipelined);
		break;
	case 5:
	case 4:
		ret = i965_write_fence_reg(obj, pipelined);
		break;
	case 3:
		ret = i915_write_fence_reg(obj, pipelined);
		break;
	case 2:
		ret = i830_write_fence_reg(obj, pipelined);
		break;
	}

	trace_i915_gem_object_get_fence(obj,
					obj->fence_reg,
					obj->tiling_mode);
	return ret;
}

/**
 * i915_gem_clear_fence_reg - clear out fence register info
 * @obj: object to clear
 *
 * Zeroes out the fence register itself and clears out the associated
 * data structures in dev_priv and obj.
 */
static void
i915_gem_clear_fence_reg(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_fence_reg *reg = &dev_priv->fence_regs[obj->fence_reg];
	uint32_t fence_reg;

	switch (INTEL_INFO(dev)->gen) {
	case 6:
		I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 +
			     (obj->fence_reg * 8), 0);
		break;
	case 5:
	case 4:
		I915_WRITE64(FENCE_REG_965_0 + (obj->fence_reg * 8), 0);
		break;
	case 3:
		if (obj->fence_reg >= 8)
			fence_reg = FENCE_REG_945_8 + (obj->fence_reg - 8) * 4;
		else
	case 2:
			fence_reg = FENCE_REG_830_0 + obj->fence_reg * 4;

		I915_WRITE(fence_reg, 0);
		break;
	}

	reg->obj = NULL;
	obj->fence_reg = I915_FENCE_REG_NONE;
	list_del_init(&reg->lru_list);
}

/**
 * i915_gem_object_put_fence_reg - waits on outstanding fenced access
 * to the buffer to finish, and then resets the fence register.
 * @obj: tiled object holding a fence register.
 * @bool: whether the wait upon the fence is interruptible
 *
 * Zeroes out the fence register itself and clears out the associated
 * data structures in dev_priv and obj.
 */
int
i915_gem_object_put_fence_reg(struct drm_i915_gem_object *obj,
			      bool interruptible)
{
	struct drm_device *dev = obj->base.dev;
	int ret;

	if (obj->fence_reg == I915_FENCE_REG_NONE)
		return 0;

	/* If we've changed tiling, GTT-mappings of the object
	 * need to re-fault to ensure that the correct fence register
	 * setup is in place.
	 */
	i915_gem_release_mmap(obj);

	/* On the i915, GPU access to tiled buffers is via a fence,
	 * therefore we must wait for any outstanding access to complete
	 * before clearing the fence.
	 */
	if (obj->fenced_gpu_access) {
		ret = i915_gem_object_flush_gpu_write_domain(obj, NULL);
		if (ret)
			return ret;

		obj->fenced_gpu_access = false;
	}

	if (obj->last_fenced_seqno) {
		ret = i915_do_wait_request(dev,
					   obj->last_fenced_seqno,
					   interruptible,
					   obj->last_fenced_ring);
		if (ret)
			return ret;

		obj->last_fenced_seqno = false;
	}

	i915_gem_object_flush_gtt_write_domain(obj);
	i915_gem_clear_fence_reg(obj);

	return 0;
}

/**
 * Finds free space in the GTT aperture and binds the object there.
 */
static int
i915_gem_object_bind_to_gtt(struct drm_i915_gem_object *obj,
			    unsigned alignment,
			    bool map_and_fenceable)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_mm_node *free_space;
	gfp_t gfpmask = __GFP_NORETRY | __GFP_NOWARN;
	u32 size, fence_size, fence_alignment, unfenced_alignment;
	bool mappable, fenceable;
	int ret;

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to bind a purgeable object\n");
		return -EINVAL;
	}

	fence_size = i915_gem_get_gtt_size(obj);
	fence_alignment = i915_gem_get_gtt_alignment(obj);
	unfenced_alignment = i915_gem_get_unfenced_gtt_alignment(obj);

	if (alignment == 0)
		alignment = map_and_fenceable ? fence_alignment :
						unfenced_alignment;
	if (map_and_fenceable && alignment & (fence_alignment - 1)) {
		DRM_ERROR("Invalid object alignment requested %u\n", alignment);
		return -EINVAL;
	}

	size = map_and_fenceable ? fence_size : obj->base.size;

	/* If the object is bigger than the entire aperture, reject it early
	 * before evicting everything in a vain attempt to find space.
	 */
	if (obj->base.size >
	    (map_and_fenceable ? dev_priv->mm.gtt_mappable_end : dev_priv->mm.gtt_total)) {
		DRM_ERROR("Attempting to bind an object larger than the aperture\n");
		return -E2BIG;
	}

 search_free:
	if (map_and_fenceable)
		free_space =
			drm_mm_search_free_in_range(&dev_priv->mm.gtt_space,
						    size, alignment, 0,
						    dev_priv->mm.gtt_mappable_end,
						    0);
	else
		free_space = drm_mm_search_free(&dev_priv->mm.gtt_space,
						size, alignment, 0);

	if (free_space != NULL) {
		if (map_and_fenceable)
			obj->gtt_space =
				drm_mm_get_block_range_generic(free_space,
							       size, alignment, 0,
							       dev_priv->mm.gtt_mappable_end,
							       0);
		else
			obj->gtt_space =
				drm_mm_get_block(free_space, size, alignment);
	}
	if (obj->gtt_space == NULL) {
		/* If the gtt is empty and we're still having trouble
		 * fitting our object in, we're out of memory.
		 */
		ret = i915_gem_evict_something(dev, size, alignment,
					       map_and_fenceable);
		if (ret)
			return ret;

		goto search_free;
	}

	ret = i915_gem_object_get_pages_gtt(obj, gfpmask);
	if (ret) {
		drm_mm_put_block(obj->gtt_space);
		obj->gtt_space = NULL;

		if (ret == -ENOMEM) {
			/* first try to clear up some space from the GTT */
			ret = i915_gem_evict_something(dev, size,
						       alignment,
						       map_and_fenceable);
			if (ret) {
				/* now try to shrink everyone else */
				if (gfpmask) {
					gfpmask = 0;
					goto search_free;
				}

				return ret;
			}

			goto search_free;
		}

		return ret;
	}

	ret = i915_gem_gtt_bind_object(obj);
	if (ret) {
		i915_gem_object_put_pages_gtt(obj);
		drm_mm_put_block(obj->gtt_space);
		obj->gtt_space = NULL;

		ret = i915_gem_evict_something(dev, size,
					       alignment, map_and_fenceable);
		if (ret)
			return ret;

		goto search_free;
	}

	obj->gtt_offset = obj->gtt_space->start;

	/* keep track of bounds object by adding it to the inactive list */
	list_add_tail(&obj->mm_list, &dev_priv->mm.inactive_list);
	i915_gem_info_add_gtt(dev_priv, obj);

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	BUG_ON(obj->base.read_domains & I915_GEM_GPU_DOMAINS);
	BUG_ON(obj->base.write_domain & I915_GEM_GPU_DOMAINS);

	trace_i915_gem_object_bind(obj, obj->gtt_offset, map_and_fenceable);

	fenceable =
		obj->gtt_space->size == fence_size &&
		(obj->gtt_space->start & (fence_alignment -1)) == 0;

	mappable =
		obj->gtt_offset + obj->base.size <= dev_priv->mm.gtt_mappable_end;

	obj->map_and_fenceable = mappable && fenceable;

	return 0;
}

void
i915_gem_clflush_object(struct drm_i915_gem_object *obj)
{
	/* If we don't have a page list set up, then we're not pinned
	 * to GPU, and we can ignore the cache flush because it'll happen
	 * again at bind time.
	 */
	if (obj->pages == NULL)
		return;

	trace_i915_gem_object_clflush(obj);

	drm_clflush_pages(obj->pages, obj->base.size / PAGE_SIZE);
}

/** Flushes any GPU write domain for the object if it's dirty. */
static int
i915_gem_object_flush_gpu_write_domain(struct drm_i915_gem_object *obj,
				       struct intel_ring_buffer *pipelined)
{
	struct drm_device *dev = obj->base.dev;

	if ((obj->base.write_domain & I915_GEM_GPU_DOMAINS) == 0)
		return 0;

	/* Queue the GPU write cache flushing we need. */
	i915_gem_flush_ring(dev, obj->ring, 0, obj->base.write_domain);
	BUG_ON(obj->base.write_domain);

	if (pipelined && pipelined == obj->ring)
		return 0;

	return i915_gem_object_wait_rendering(obj, true);
}

/** Flushes the GTT write domain for the object if it's dirty. */
static void
i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_GTT)
		return;

	/* No actual flushing is required for the GTT write domain.   Writes
	 * to it immediately go to main memory as far as we know, so there's
	 * no chipset flush.  It also doesn't land in render cache.
	 */
	i915_gem_release_mmap(obj);

	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
}

/** Flushes the CPU write domain for the object if it's dirty. */
static void
i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU)
		return;

	i915_gem_clflush_object(obj);
	intel_gtt_chipset_flush();
	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
}

/**
 * Moves a single object to the GTT read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj, int write)
{
	uint32_t old_write_domain, old_read_domains;
	int ret;

	/* Not valid to be called on unbound objects. */
	if (obj->gtt_space == NULL)
		return -EINVAL;

	ret = i915_gem_object_flush_gpu_write_domain(obj, NULL);
	if (ret != 0)
		return ret;

	i915_gem_object_flush_cpu_write_domain(obj);

	if (write) {
		ret = i915_gem_object_wait_rendering(obj, true);
		if (ret)
			return ret;
	}

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_GTT;
		obj->base.write_domain = I915_GEM_DOMAIN_GTT;
		obj->dirty = 1;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return 0;
}

/*
 * Prepare buffer for display plane. Use uninterruptible for possible flush
 * wait, as in modesetting process we're not supposed to be interrupted.
 */
int
i915_gem_object_set_to_display_plane(struct drm_i915_gem_object *obj,
				     struct intel_ring_buffer *pipelined)
{
	uint32_t old_read_domains;
	int ret;

	/* Not valid to be called on unbound objects. */
	if (obj->gtt_space == NULL)
		return -EINVAL;

	ret = i915_gem_object_flush_gpu_write_domain(obj, pipelined);
	if (ret)
		return ret;

	/* Currently, we are always called from an non-interruptible context. */
	if (!pipelined) {
		ret = i915_gem_object_wait_rendering(obj, false);
		if (ret)
			return ret;
	}

	i915_gem_object_flush_cpu_write_domain(obj);

	old_read_domains = obj->base.read_domains;
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    obj->base.write_domain);

	return 0;
}

int
i915_gem_object_flush_gpu(struct drm_i915_gem_object *obj,
			  bool interruptible)
{
	if (!obj->active)
		return 0;

	if (obj->base.write_domain & I915_GEM_GPU_DOMAINS)
		i915_gem_flush_ring(obj->base.dev, obj->ring,
				    0, obj->base.write_domain);

	return i915_gem_object_wait_rendering(obj, interruptible);
}

/**
 * Moves a single object to the CPU read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
static int
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write)
{
	uint32_t old_write_domain, old_read_domains;
	int ret;

	ret = i915_gem_object_flush_gpu_write_domain(obj, false);
	if (ret != 0)
		return ret;

	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we have a partially-valid cache of the object in the CPU,
	 * finish invalidating it and free the per-page flags.
	 */
	i915_gem_object_set_to_full_cpu_read_domain(obj);

	if (write) {
		ret = i915_gem_object_wait_rendering(obj, true);
		if (ret)
			return ret;
	}

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj);

		obj->base.read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return 0;
}

/*
 * Set the next domain for the specified object. This
 * may not actually perform the necessary flushing/invaliding though,
 * as that may want to be batched with other set_domain operations
 *
 * This is (we hope) the only really tricky part of gem. The goal
 * is fairly simple -- track which caches hold bits of the object
 * and make sure they remain coherent. A few concrete examples may
 * help to explain how it works. For shorthand, we use the notation
 * (read_domains, write_domain), e.g. (CPU, CPU) to indicate the
 * a pair of read and write domain masks.
 *
 * Case 1: the batch buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Mapped to GTT
 *	4. Read by GPU
 *	5. Unmapped from GTT
 *	6. Freed
 *
 *	Let's take these a step at a time
 *
 *	1. Allocated
 *		Pages allocated from the kernel may still have
 *		cache contents, so we set them to (CPU, CPU) always.
 *	2. Written by CPU (using pwrite)
 *		The pwrite function calls set_domain (CPU, CPU) and
 *		this function does nothing (as nothing changes)
 *	3. Mapped by GTT
 *		This function asserts that the object is not
 *		currently in any GPU-based read or write domains
 *	4. Read by GPU
 *		i915_gem_execbuffer calls set_domain (COMMAND, 0).
 *		As write_domain is zero, this function adds in the
 *		current read domains (CPU+COMMAND, 0).
 *		flush_domains is set to CPU.
 *		invalidate_domains is set to COMMAND
 *		clflush is run to get data out of the CPU caches
 *		then i915_dev_set_domain calls i915_gem_flush to
 *		emit an MI_FLUSH and drm_agp_chipset_flush
 *	5. Unmapped from GTT
 *		i915_gem_object_unbind calls set_domain (CPU, CPU)
 *		flush_domains and invalidate_domains end up both zero
 *		so no flushing/invalidating happens
 *	6. Freed
 *		yay, done
 *
 * Case 2: The shared render buffer
 *
 *	1. Allocated
 *	2. Mapped to GTT
 *	3. Read/written by GPU
 *	4. set_domain to (CPU,CPU)
 *	5. Read/written by CPU
 *	6. Read/written by GPU
 *
 *	1. Allocated
 *		Same as last example, (CPU, CPU)
 *	2. Mapped to GTT
 *		Nothing changes (assertions find that it is not in the GPU)
 *	3. Read/written by GPU
 *		execbuffer calls set_domain (RENDER, RENDER)
 *		flush_domains gets CPU
 *		invalidate_domains gets GPU
 *		clflush (obj)
 *		MI_FLUSH and drm_agp_chipset_flush
 *	4. set_domain (CPU, CPU)
 *		flush_domains gets GPU
 *		invalidate_domains gets CPU
 *		wait_rendering (obj) to make sure all drawing is complete.
 *		This will include an MI_FLUSH to get the data from GPU
 *		to memory
 *		clflush (obj) to invalidate the CPU cache
 *		Another MI_FLUSH in i915_gem_flush (eliminate this somehow?)
 *	5. Read/written by CPU
 *		cache lines are loaded and dirtied
 *	6. Read written by GPU
 *		Same as last GPU access
 *
 * Case 3: The constant buffer
 *
 *	1. Allocated
 *	2. Written by CPU
 *	3. Read by GPU
 *	4. Updated (written) by CPU again
 *	5. Read by GPU
 *
 *	1. Allocated
 *		(CPU, CPU)
 *	2. Written by CPU
 *		(CPU, CPU)
 *	3. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 *	4. Updated (written) by CPU again
 *		(CPU, CPU)
 *		flush_domains = 0 (no previous write domain)
 *		invalidate_domains = 0 (no new read domains)
 *	5. Read by GPU
 *		(CPU+RENDER, 0)
 *		flush_domains = CPU
 *		invalidate_domains = RENDER
 *		clflush (obj)
 *		MI_FLUSH
 *		drm_agp_chipset_flush
 */
static void
i915_gem_object_set_to_gpu_domain(struct drm_i915_gem_object *obj,
				  struct intel_ring_buffer *ring,
				  struct change_domains *cd)
{
	uint32_t invalidate_domains = 0, flush_domains = 0;

	/*
	 * If the object isn't moving to a new write domain,
	 * let the object stay in multiple read domains
	 */
	if (obj->base.pending_write_domain == 0)
		obj->base.pending_read_domains |= obj->base.read_domains;

	/*
	 * Flush the current write domain if
	 * the new read domains don't match. Invalidate
	 * any read domains which differ from the old
	 * write domain
	 */
	if (obj->base.write_domain &&
	    (((obj->base.write_domain != obj->base.pending_read_domains ||
	       obj->ring != ring)) ||
	     (obj->fenced_gpu_access && !obj->pending_fenced_gpu_access))) {
		flush_domains |= obj->base.write_domain;
		invalidate_domains |=
			obj->base.pending_read_domains & ~obj->base.write_domain;
	}
	/*
	 * Invalidate any read caches which may have
	 * stale data. That is, any new read domains.
	 */
	invalidate_domains |= obj->base.pending_read_domains & ~obj->base.read_domains;
	if ((flush_domains | invalidate_domains) & I915_GEM_DOMAIN_CPU)
		i915_gem_clflush_object(obj);

	/* blow away mappings if mapped through GTT */
	if ((flush_domains | invalidate_domains) & I915_GEM_DOMAIN_GTT)
		i915_gem_release_mmap(obj);

	/* The actual obj->write_domain will be updated with
	 * pending_write_domain after we emit the accumulated flush for all
	 * of our domain changes in execbuffers (which clears objects'
	 * write_domains).  So if we have a current write domain that we
	 * aren't changing, set pending_write_domain to that.
	 */
	if (flush_domains == 0 && obj->base.pending_write_domain == 0)
		obj->base.pending_write_domain = obj->base.write_domain;

	cd->invalidate_domains |= invalidate_domains;
	cd->flush_domains |= flush_domains;
	if (flush_domains & I915_GEM_GPU_DOMAINS)
		cd->flush_rings |= obj->ring->id;
	if (invalidate_domains & I915_GEM_GPU_DOMAINS)
		cd->flush_rings |= ring->id;
}

/**
 * Moves the object from a partially CPU read to a full one.
 *
 * Note that this only resolves i915_gem_object_set_cpu_read_domain_range(),
 * and doesn't handle transitioning from !(read_domains & I915_GEM_DOMAIN_CPU).
 */
static void
i915_gem_object_set_to_full_cpu_read_domain(struct drm_i915_gem_object *obj)
{
	if (!obj->page_cpu_valid)
		return;

	/* If we're partially in the CPU read domain, finish moving it in.
	 */
	if (obj->base.read_domains & I915_GEM_DOMAIN_CPU) {
		int i;

		for (i = 0; i <= (obj->base.size - 1) / PAGE_SIZE; i++) {
			if (obj->page_cpu_valid[i])
				continue;
			drm_clflush_pages(obj->pages + i, 1);
		}
	}

	/* Free the page_cpu_valid mappings which are now stale, whether
	 * or not we've got I915_GEM_DOMAIN_CPU.
	 */
	kfree(obj->page_cpu_valid);
	obj->page_cpu_valid = NULL;
}

/**
 * Set the CPU read domain on a range of the object.
 *
 * The object ends up with I915_GEM_DOMAIN_CPU in its read flags although it's
 * not entirely valid.  The page_cpu_valid member of the object flags which
 * pages have been flushed, and will be respected by
 * i915_gem_object_set_to_cpu_domain() if it's called on to get a valid mapping
 * of the whole object.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
static int
i915_gem_object_set_cpu_read_domain_range(struct drm_i915_gem_object *obj,
					  uint64_t offset, uint64_t size)
{
	uint32_t old_read_domains;
	int i, ret;

	if (offset == 0 && size == obj->base.size)
		return i915_gem_object_set_to_cpu_domain(obj, 0);

	ret = i915_gem_object_flush_gpu_write_domain(obj, false);
	if (ret != 0)
		return ret;
	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we're already fully in the CPU read domain, we're done. */
	if (obj->page_cpu_valid == NULL &&
	    (obj->base.read_domains & I915_GEM_DOMAIN_CPU) != 0)
		return 0;

	/* Otherwise, create/clear the per-page CPU read domain flag if we're
	 * newly adding I915_GEM_DOMAIN_CPU
	 */
	if (obj->page_cpu_valid == NULL) {
		obj->page_cpu_valid = kzalloc(obj->base.size / PAGE_SIZE,
					      GFP_KERNEL);
		if (obj->page_cpu_valid == NULL)
			return -ENOMEM;
	} else if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0)
		memset(obj->page_cpu_valid, 0, obj->base.size / PAGE_SIZE);

	/* Flush the cache on any pages that are still invalid from the CPU's
	 * perspective.
	 */
	for (i = offset / PAGE_SIZE; i <= (offset + size - 1) / PAGE_SIZE;
	     i++) {
		if (obj->page_cpu_valid[i])
			continue;

		drm_clflush_pages(obj->pages + i, 1);

		obj->page_cpu_valid[i] = 1;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	old_read_domains = obj->base.read_domains;
	obj->base.read_domains |= I915_GEM_DOMAIN_CPU;

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    obj->base.write_domain);

	return 0;
}

static int
i915_gem_execbuffer_relocate_entry(struct drm_i915_gem_object *obj,
				   struct drm_file *file_priv,
				   struct drm_i915_gem_exec_object2 *entry,
				   struct drm_i915_gem_relocation_entry *reloc)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_object *target_obj;
	uint32_t target_offset;
	int ret = -EINVAL;

	target_obj = drm_gem_object_lookup(dev, file_priv,
					   reloc->target_handle);
	if (target_obj == NULL)
		return -ENOENT;

	target_offset = to_intel_bo(target_obj)->gtt_offset;

#if WATCH_RELOC
	DRM_INFO("%s: obj %p offset %08x target %d "
		 "read %08x write %08x gtt %08x "
		 "presumed %08x delta %08x\n",
		 __func__,
		 obj,
		 (int) reloc->offset,
		 (int) reloc->target_handle,
		 (int) reloc->read_domains,
		 (int) reloc->write_domain,
		 (int) target_offset,
		 (int) reloc->presumed_offset,
		 reloc->delta);
#endif

	/* The target buffer should have appeared before us in the
	 * exec_object list, so it should have a GTT space bound by now.
	 */
	if (target_offset == 0) {
		DRM_ERROR("No GTT space found for object %d\n",
			  reloc->target_handle);
		goto err;
	}

	/* Validate that the target is in a valid r/w GPU domain */
	if (reloc->write_domain & (reloc->write_domain - 1)) {
		DRM_ERROR("reloc with multiple write domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		goto err;
	}
	if (reloc->write_domain & I915_GEM_DOMAIN_CPU ||
	    reloc->read_domains & I915_GEM_DOMAIN_CPU) {
		DRM_ERROR("reloc with read/write CPU domains: "
			  "obj %p target %d offset %d "
			  "read %08x write %08x",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->read_domains,
			  reloc->write_domain);
		goto err;
	}
	if (reloc->write_domain && target_obj->pending_write_domain &&
	    reloc->write_domain != target_obj->pending_write_domain) {
		DRM_ERROR("Write domain conflict: "
			  "obj %p target %d offset %d "
			  "new %08x old %08x\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  reloc->write_domain,
			  target_obj->pending_write_domain);
		goto err;
	}

	target_obj->pending_read_domains |= reloc->read_domains;
	target_obj->pending_write_domain |= reloc->write_domain;

	/* If the relocation already has the right value in it, no
	 * more work needs to be done.
	 */
	if (target_offset == reloc->presumed_offset)
		goto out;

	/* Check that the relocation address is valid... */
	if (reloc->offset > obj->base.size - 4) {
		DRM_ERROR("Relocation beyond object bounds: "
			  "obj %p target %d offset %d size %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset,
			  (int) obj->base.size);
		goto err;
	}
	if (reloc->offset & 3) {
		DRM_ERROR("Relocation not 4-byte aligned: "
			  "obj %p target %d offset %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->offset);
		goto err;
	}

	/* and points to somewhere within the target object. */
	if (reloc->delta >= target_obj->size) {
		DRM_ERROR("Relocation beyond target object bounds: "
			  "obj %p target %d delta %d size %d.\n",
			  obj, reloc->target_handle,
			  (int) reloc->delta,
			  (int) target_obj->size);
		goto err;
	}

	reloc->delta += target_offset;
	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU) {
		uint32_t page_offset = reloc->offset & ~PAGE_MASK;
		char *vaddr;

		vaddr = kmap_atomic(obj->pages[reloc->offset >> PAGE_SHIFT]);
		*(uint32_t *)(vaddr + page_offset) = reloc->delta;
		kunmap_atomic(vaddr);
	} else {
		struct drm_i915_private *dev_priv = dev->dev_private;
		uint32_t __iomem *reloc_entry;
		void __iomem *reloc_page;

		ret = i915_gem_object_set_to_gtt_domain(obj, 1);
		if (ret)
			goto err;

		/* Map the page containing the relocation we're going to perform.  */
		reloc->offset += obj->gtt_offset;
		reloc_page = io_mapping_map_atomic_wc(dev_priv->mm.gtt_mapping,
						      reloc->offset & PAGE_MASK);
		reloc_entry = (uint32_t __iomem *)
			(reloc_page + (reloc->offset & ~PAGE_MASK));
		iowrite32(reloc->delta, reloc_entry);
		io_mapping_unmap_atomic(reloc_page);
	}

	/* and update the user's relocation entry */
	reloc->presumed_offset = target_offset;

out:
	ret = 0;
err:
	drm_gem_object_unreference(target_obj);
	return ret;
}

static int
i915_gem_execbuffer_relocate_object(struct drm_i915_gem_object *obj,
				    struct drm_file *file_priv,
				    struct drm_i915_gem_exec_object2 *entry)
{
	struct drm_i915_gem_relocation_entry __user *user_relocs;
	int i, ret;

	user_relocs = (void __user *)(uintptr_t)entry->relocs_ptr;
	for (i = 0; i < entry->relocation_count; i++) {
		struct drm_i915_gem_relocation_entry reloc;

		if (__copy_from_user_inatomic(&reloc,
					      user_relocs+i,
					      sizeof(reloc)))
			return -EFAULT;

		ret = i915_gem_execbuffer_relocate_entry(obj, file_priv, entry, &reloc);
		if (ret)
			return ret;

		if (__copy_to_user_inatomic(&user_relocs[i].presumed_offset,
					    &reloc.presumed_offset,
					    sizeof(reloc.presumed_offset)))
			return -EFAULT;
	}

	return 0;
}

static int
i915_gem_execbuffer_relocate_object_slow(struct drm_i915_gem_object *obj,
					 struct drm_file *file_priv,
					 struct drm_i915_gem_exec_object2 *entry,
					 struct drm_i915_gem_relocation_entry *relocs)
{
	int i, ret;

	for (i = 0; i < entry->relocation_count; i++) {
		ret = i915_gem_execbuffer_relocate_entry(obj, file_priv, entry, &relocs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int
i915_gem_execbuffer_relocate(struct drm_device *dev,
			     struct drm_file *file,
			     struct drm_i915_gem_object **object_list,
			     struct drm_i915_gem_exec_object2 *exec_list,
			     int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		struct drm_i915_gem_object *obj = object_list[i];
		obj->base.pending_read_domains = 0;
		obj->base.pending_write_domain = 0;
		ret = i915_gem_execbuffer_relocate_object(obj, file,
							  &exec_list[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int
i915_gem_execbuffer_reserve(struct drm_device *dev,
			    struct drm_file *file,
			    struct drm_i915_gem_object **object_list,
			    struct drm_i915_gem_exec_object2 *exec_list,
			    int count)
{
	int ret, i, retry;

	/* Attempt to pin all of the buffers into the GTT.
	 * This is done in 3 phases:
	 *
	 * 1a. Unbind all objects that do not match the GTT constraints for
	 *     the execbuffer (fenceable, mappable, alignment etc).
	 * 1b. Increment pin count for already bound objects.
	 * 2.  Bind new objects.
	 * 3.  Decrement pin count.
	 *
	 * This avoid unnecessary unbinding of later objects in order to makr
	 * room for the earlier objects *unless* we need to defragment.
	 */
	retry = 0;
	do {
		ret = 0;

		/* Unbind any ill-fitting objects or pin. */
		for (i = 0; i < count; i++) {
			struct drm_i915_gem_object *obj = object_list[i];
			struct drm_i915_gem_exec_object2 *entry = &exec_list[i];
			bool need_fence, need_mappable;

			if (!obj->gtt_space)
				continue;

			need_fence =
				entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				obj->tiling_mode != I915_TILING_NONE;
			need_mappable =
				entry->relocation_count ? true : need_fence;

			if ((entry->alignment && obj->gtt_offset & (entry->alignment - 1)) ||
			    (need_mappable && !obj->map_and_fenceable))
				ret = i915_gem_object_unbind(obj);
			else
				ret = i915_gem_object_pin(obj,
							  entry->alignment,
							  need_mappable);
			if (ret) {
				count = i;
				goto err;
			}
		}

		/* Bind fresh objects */
		for (i = 0; i < count; i++) {
			struct drm_i915_gem_exec_object2 *entry = &exec_list[i];
			struct drm_i915_gem_object *obj = object_list[i];
			bool need_fence;

			need_fence =
				entry->flags & EXEC_OBJECT_NEEDS_FENCE &&
				obj->tiling_mode != I915_TILING_NONE;

			if (!obj->gtt_space) {
				bool need_mappable =
					entry->relocation_count ? true : need_fence;

				ret = i915_gem_object_pin(obj,
							  entry->alignment,
							  need_mappable);
				if (ret)
					break;
			}

			if (need_fence) {
				ret = i915_gem_object_get_fence_reg(obj, true);
				if (ret)
					break;

				obj->pending_fenced_gpu_access = true;
			}

			entry->offset = obj->gtt_offset;
		}

err:		/* Decrement pin count for bound objects */
		for (i = 0; i < count; i++) {
			struct drm_i915_gem_object *obj = object_list[i];
			if (obj->gtt_space)
				i915_gem_object_unpin(obj);
		}

		if (ret != -ENOSPC || retry > 1)
			return ret;

		/* First attempt, just clear anything that is purgeable.
		 * Second attempt, clear the entire GTT.
		 */
		ret = i915_gem_evict_everything(dev, retry == 0);
		if (ret)
			return ret;

		retry++;
	} while (1);
}

static int
i915_gem_execbuffer_relocate_slow(struct drm_device *dev,
				  struct drm_file *file,
				  struct drm_i915_gem_object **object_list,
				  struct drm_i915_gem_exec_object2 *exec_list,
				  int count)
{
	struct drm_i915_gem_relocation_entry *reloc;
	int i, total, ret;

	for (i = 0; i < count; i++)
		object_list[i]->in_execbuffer = false;

	mutex_unlock(&dev->struct_mutex);

	total = 0;
	for (i = 0; i < count; i++)
		total += exec_list[i].relocation_count;

	reloc = drm_malloc_ab(total, sizeof(*reloc));
	if (reloc == NULL) {
		mutex_lock(&dev->struct_mutex);
		return -ENOMEM;
	}

	total = 0;
	for (i = 0; i < count; i++) {
		struct drm_i915_gem_relocation_entry __user *user_relocs;

		user_relocs = (void __user *)(uintptr_t)exec_list[i].relocs_ptr;

		if (copy_from_user(reloc+total, user_relocs,
				   exec_list[i].relocation_count *
				   sizeof(*reloc))) {
			ret = -EFAULT;
			mutex_lock(&dev->struct_mutex);
			goto err;
		}

		total += exec_list[i].relocation_count;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret) {
		mutex_lock(&dev->struct_mutex);
		goto err;
	}

	ret = i915_gem_execbuffer_reserve(dev, file,
					  object_list, exec_list,
					  count);
	if (ret)
		goto err;

	total = 0;
	for (i = 0; i < count; i++) {
		struct drm_i915_gem_object *obj = object_list[i];
		obj->base.pending_read_domains = 0;
		obj->base.pending_write_domain = 0;
		ret = i915_gem_execbuffer_relocate_object_slow(obj, file,
							       &exec_list[i],
							       reloc + total);
		if (ret)
			goto err;

		total += exec_list[i].relocation_count;
	}

	/* Leave the user relocations as are, this is the painfully slow path,
	 * and we want to avoid the complication of dropping the lock whilst
	 * having buffers reserved in the aperture and so causing spurious
	 * ENOSPC for random operations.
	 */

err:
	drm_free_large(reloc);
	return ret;
}

static int
i915_gem_execbuffer_move_to_gpu(struct drm_device *dev,
				struct drm_file *file,
				struct intel_ring_buffer *ring,
				struct drm_i915_gem_object **objects,
				int count)
{
	struct change_domains cd;
	int ret, i;

	cd.invalidate_domains = 0;
	cd.flush_domains = 0;
	cd.flush_rings = 0;
	for (i = 0; i < count; i++)
		i915_gem_object_set_to_gpu_domain(objects[i], ring, &cd);

	if (cd.invalidate_domains | cd.flush_domains) {
#if WATCH_EXEC
		DRM_INFO("%s: invalidate_domains %08x flush_domains %08x\n",
			  __func__,
			 cd.invalidate_domains,
			 cd.flush_domains);
#endif
		i915_gem_flush(dev,
			       cd.invalidate_domains,
			       cd.flush_domains,
			       cd.flush_rings);
	}

	for (i = 0; i < count; i++) {
		struct drm_i915_gem_object *obj = objects[i];
		/* XXX replace with semaphores */
		if (obj->ring && ring != obj->ring) {
			ret = i915_gem_object_wait_rendering(obj, true);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
static int
i915_gem_ring_throttle(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	unsigned long recent_enough = jiffies - msecs_to_jiffies(20);
	struct drm_i915_gem_request *request;
	struct intel_ring_buffer *ring = NULL;
	u32 seqno = 0;
	int ret;

	spin_lock(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_list) {
		if (time_after_eq(request->emitted_jiffies, recent_enough))
			break;

		ring = request->ring;
		seqno = request->seqno;
	}
	spin_unlock(&file_priv->mm.lock);

	if (seqno == 0)
		return 0;

	ret = 0;
	if (!i915_seqno_passed(ring->get_seqno(ring), seqno)) {
		/* And wait for the seqno passing without holding any locks and
		 * causing extra latency for others. This is safe as the irq
		 * generation is designed to be run atomically and so is
		 * lockless.
		 */
		ring->user_irq_get(ring);
		ret = wait_event_interruptible(ring->irq_queue,
					       i915_seqno_passed(ring->get_seqno(ring), seqno)
					       || atomic_read(&dev_priv->mm.wedged));
		ring->user_irq_put(ring);

		if (ret == 0 && atomic_read(&dev_priv->mm.wedged))
			ret = -EIO;
	}

	if (ret == 0)
		queue_delayed_work(dev_priv->wq, &dev_priv->mm.retire_work, 0);

	return ret;
}

static int
i915_gem_check_execbuffer(struct drm_i915_gem_execbuffer2 *exec,
			  uint64_t exec_offset)
{
	uint32_t exec_start, exec_len;

	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	exec_len = (uint32_t) exec->batch_len;

	if ((exec_start | exec_len) & 0x7)
		return -EINVAL;

	if (!exec_start)
		return -EINVAL;

	return 0;
}

static int
validate_exec_list(struct drm_i915_gem_exec_object2 *exec,
		   int count)
{
	int i;

	for (i = 0; i < count; i++) {
		char __user *ptr = (char __user *)(uintptr_t)exec[i].relocs_ptr;
		int length; /* limited by fault_in_pages_readable() */

		/* First check for malicious input causing overflow */
		if (exec[i].relocation_count >
		    INT_MAX / sizeof(struct drm_i915_gem_relocation_entry))
			return -EINVAL;

		length = exec[i].relocation_count *
			sizeof(struct drm_i915_gem_relocation_entry);
		if (!access_ok(VERIFY_READ, ptr, length))
			return -EFAULT;

		/* we may also need to update the presumed offsets */
		if (!access_ok(VERIFY_WRITE, ptr, length))
			return -EFAULT;

		if (fault_in_pages_readable(ptr, length))
			return -EFAULT;
	}

	return 0;
}

static int
i915_gem_do_execbuffer(struct drm_device *dev, void *data,
		       struct drm_file *file,
		       struct drm_i915_gem_execbuffer2 *args,
		       struct drm_i915_gem_exec_object2 *exec_list)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object **object_list = NULL;
	struct drm_i915_gem_object *batch_obj;
	struct drm_clip_rect *cliprects = NULL;
	struct drm_i915_gem_request *request = NULL;
	int ret, i, flips;
	uint64_t exec_offset;

	struct intel_ring_buffer *ring = NULL;

	ret = i915_gem_check_is_wedged(dev);
	if (ret)
		return ret;

	ret = validate_exec_list(exec_list, args->buffer_count);
	if (ret)
		return ret;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif
	switch (args->flags & I915_EXEC_RING_MASK) {
	case I915_EXEC_DEFAULT:
	case I915_EXEC_RENDER:
		ring = &dev_priv->render_ring;
		break;
	case I915_EXEC_BSD:
		if (!HAS_BSD(dev)) {
			DRM_ERROR("execbuf with invalid ring (BSD)\n");
			return -EINVAL;
		}
		ring = &dev_priv->bsd_ring;
		break;
	case I915_EXEC_BLT:
		if (!HAS_BLT(dev)) {
			DRM_ERROR("execbuf with invalid ring (BLT)\n");
			return -EINVAL;
		}
		ring = &dev_priv->blt_ring;
		break;
	default:
		DRM_ERROR("execbuf with unknown ring: %d\n",
			  (int)(args->flags & I915_EXEC_RING_MASK));
		return -EINVAL;
	}

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}
	object_list = drm_malloc_ab(sizeof(*object_list), args->buffer_count);
	if (object_list == NULL) {
		DRM_ERROR("Failed to allocate object list for %d buffers\n",
			  args->buffer_count);
		ret = -ENOMEM;
		goto pre_mutex_err;
	}

	if (args->num_cliprects != 0) {
		cliprects = kcalloc(args->num_cliprects, sizeof(*cliprects),
				    GFP_KERNEL);
		if (cliprects == NULL) {
			ret = -ENOMEM;
			goto pre_mutex_err;
		}

		ret = copy_from_user(cliprects,
				     (struct drm_clip_rect __user *)
				     (uintptr_t) args->cliprects_ptr,
				     sizeof(*cliprects) * args->num_cliprects);
		if (ret != 0) {
			DRM_ERROR("copy %d cliprects failed: %d\n",
				  args->num_cliprects, ret);
			ret = -EFAULT;
			goto pre_mutex_err;
		}
	}

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (request == NULL) {
		ret = -ENOMEM;
		goto pre_mutex_err;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto pre_mutex_err;

	if (dev_priv->mm.suspended) {
		mutex_unlock(&dev->struct_mutex);
		ret = -EBUSY;
		goto pre_mutex_err;
	}

	/* Look up object handles */
	for (i = 0; i < args->buffer_count; i++) {
		struct drm_i915_gem_object *obj;

		obj = to_intel_bo (drm_gem_object_lookup(dev, file,
							 exec_list[i].handle));
		if (obj == NULL) {
			DRM_ERROR("Invalid object handle %d at index %d\n",
				   exec_list[i].handle, i);
			/* prevent error path from reading uninitialized data */
			args->buffer_count = i;
			ret = -ENOENT;
			goto err;
		}
		object_list[i] = obj;

		if (obj->in_execbuffer) {
			DRM_ERROR("Object %p appears more than once in object list\n",
				   obj);
			/* prevent error path from reading uninitialized data */
			args->buffer_count = i + 1;
			ret = -EINVAL;
			goto err;
		}
		obj->in_execbuffer = true;
		obj->pending_fenced_gpu_access = false;
	}

	/* Move the objects en-masse into the GTT, evicting if necessary. */
	ret = i915_gem_execbuffer_reserve(dev, file,
					  object_list, exec_list,
					  args->buffer_count);
	if (ret)
		goto err;

	/* The objects are in their final locations, apply the relocations. */
	ret = i915_gem_execbuffer_relocate(dev, file,
					   object_list, exec_list,
					   args->buffer_count);
	if (ret) {
		if (ret == -EFAULT) {
			ret = i915_gem_execbuffer_relocate_slow(dev, file,
								object_list,
								exec_list,
								args->buffer_count);
			BUG_ON(!mutex_is_locked(&dev->struct_mutex));
		}
		if (ret)
			goto err;
	}

	/* Set the pending read domains for the batch buffer to COMMAND */
	batch_obj = object_list[args->buffer_count-1];
	if (batch_obj->base.pending_write_domain) {
		DRM_ERROR("Attempting to use self-modifying batch buffer\n");
		ret = -EINVAL;
		goto err;
	}
	batch_obj->base.pending_read_domains |= I915_GEM_DOMAIN_COMMAND;

	/* Sanity check the batch buffer */
	exec_offset = batch_obj->gtt_offset;
	ret = i915_gem_check_execbuffer(args, exec_offset);
	if (ret != 0) {
		DRM_ERROR("execbuf with invalid offset/length\n");
		goto err;
	}

	ret = i915_gem_execbuffer_move_to_gpu(dev, file, ring,
					      object_list, args->buffer_count);
	if (ret)
		goto err;

#if WATCH_COHERENCY
	for (i = 0; i < args->buffer_count; i++) {
		i915_gem_object_check_coherency(object_list[i],
						exec_list[i].handle);
	}
#endif

#if WATCH_EXEC
	i915_gem_dump_object(batch_obj,
			      args->batch_len,
			      __func__,
			      ~0);
#endif

	/* Check for any pending flips. As we only maintain a flip queue depth
	 * of 1, we can simply insert a WAIT for the next display flip prior
	 * to executing the batch and avoid stalling the CPU.
	 */
	flips = 0;
	for (i = 0; i < args->buffer_count; i++) {
		if (object_list[i]->base.write_domain)
			flips |= atomic_read(&object_list[i]->pending_flip);
	}
	if (flips) {
		int plane, flip_mask;

		for (plane = 0; flips >> plane; plane++) {
			if (((flips >> plane) & 1) == 0)
				continue;

			if (plane)
				flip_mask = MI_WAIT_FOR_PLANE_B_FLIP;
			else
				flip_mask = MI_WAIT_FOR_PLANE_A_FLIP;

			ret = intel_ring_begin(ring, 2);
			if (ret)
				goto err;

			intel_ring_emit(ring, MI_WAIT_FOR_EVENT | flip_mask);
			intel_ring_emit(ring, MI_NOOP);
			intel_ring_advance(ring);
		}
	}

	/* Exec the batchbuffer */
	ret = ring->dispatch_execbuffer(ring, args, cliprects, exec_offset);
	if (ret) {
		DRM_ERROR("dispatch failed %d\n", ret);
		goto err;
	}

	for (i = 0; i < args->buffer_count; i++) {
		struct drm_i915_gem_object *obj = object_list[i];

		obj->base.read_domains = obj->base.pending_read_domains;
		obj->base.write_domain = obj->base.pending_write_domain;
		obj->fenced_gpu_access = obj->pending_fenced_gpu_access;

		i915_gem_object_move_to_active(obj, ring);
		if (obj->base.write_domain) {
			obj->dirty = 1;
			list_move_tail(&obj->gpu_write_list,
				       &ring->gpu_write_list);
			intel_mark_busy(dev, obj);
		}

		trace_i915_gem_object_change_domain(obj,
						    obj->base.read_domains,
						    obj->base.write_domain);
	}

	/*
	 * Ensure that the commands in the batch buffer are
	 * finished before the interrupt fires
	 */
	i915_retire_commands(dev, ring);

	if (i915_add_request(dev, file, request, ring))
		i915_gem_next_request_seqno(dev, ring);
	else
		request = NULL;

err:
	for (i = 0; i < args->buffer_count; i++) {
		object_list[i]->in_execbuffer = false;
		drm_gem_object_unreference(&object_list[i]->base);
	}

	mutex_unlock(&dev->struct_mutex);

pre_mutex_err:
	drm_free_large(object_list);
	kfree(cliprects);
	kfree(request);

	return ret;
}

/*
 * Legacy execbuffer just creates an exec2 list from the original exec object
 * list array and passes it to the real function.
 */
int
i915_gem_execbuffer(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_execbuffer *args = data;
	struct drm_i915_gem_execbuffer2 exec2;
	struct drm_i915_gem_exec_object *exec_list = NULL;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret, i;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	/* Copy in the exec list from userland */
	exec_list = drm_malloc_ab(sizeof(*exec_list), args->buffer_count);
	exec2_list = drm_malloc_ab(sizeof(*exec2_list), args->buffer_count);
	if (exec_list == NULL || exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		drm_free_large(exec_list);
		drm_free_large(exec2_list);
		return -ENOMEM;
	}
	ret = copy_from_user(exec_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free_large(exec_list);
		drm_free_large(exec2_list);
		return -EFAULT;
	}

	for (i = 0; i < args->buffer_count; i++) {
		exec2_list[i].handle = exec_list[i].handle;
		exec2_list[i].relocation_count = exec_list[i].relocation_count;
		exec2_list[i].relocs_ptr = exec_list[i].relocs_ptr;
		exec2_list[i].alignment = exec_list[i].alignment;
		exec2_list[i].offset = exec_list[i].offset;
		if (INTEL_INFO(dev)->gen < 4)
			exec2_list[i].flags = EXEC_OBJECT_NEEDS_FENCE;
		else
			exec2_list[i].flags = 0;
	}

	exec2.buffers_ptr = args->buffers_ptr;
	exec2.buffer_count = args->buffer_count;
	exec2.batch_start_offset = args->batch_start_offset;
	exec2.batch_len = args->batch_len;
	exec2.DR1 = args->DR1;
	exec2.DR4 = args->DR4;
	exec2.num_cliprects = args->num_cliprects;
	exec2.cliprects_ptr = args->cliprects_ptr;
	exec2.flags = I915_EXEC_RENDER;

	ret = i915_gem_do_execbuffer(dev, data, file, &exec2, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		for (i = 0; i < args->buffer_count; i++)
			exec_list[i].offset = exec2_list[i].offset;
		/* ... and back out to userspace */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec_list,
				   sizeof(*exec_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free_large(exec_list);
	drm_free_large(exec2_list);
	return ret;
}

int
i915_gem_execbuffer2(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_execbuffer2 *args = data;
	struct drm_i915_gem_exec_object2 *exec2_list = NULL;
	int ret;

#if WATCH_EXEC
	DRM_INFO("buffers_ptr %d buffer_count %d len %08x\n",
		  (int) args->buffers_ptr, args->buffer_count, args->batch_len);
#endif

	if (args->buffer_count < 1) {
		DRM_ERROR("execbuf2 with %d buffers\n", args->buffer_count);
		return -EINVAL;
	}

	exec2_list = drm_malloc_ab(sizeof(*exec2_list), args->buffer_count);
	if (exec2_list == NULL) {
		DRM_ERROR("Failed to allocate exec list for %d buffers\n",
			  args->buffer_count);
		return -ENOMEM;
	}
	ret = copy_from_user(exec2_list,
			     (struct drm_i915_relocation_entry __user *)
			     (uintptr_t) args->buffers_ptr,
			     sizeof(*exec2_list) * args->buffer_count);
	if (ret != 0) {
		DRM_ERROR("copy %d exec entries failed %d\n",
			  args->buffer_count, ret);
		drm_free_large(exec2_list);
		return -EFAULT;
	}

	ret = i915_gem_do_execbuffer(dev, data, file, args, exec2_list);
	if (!ret) {
		/* Copy the new buffer offsets back to the user's exec list. */
		ret = copy_to_user((struct drm_i915_relocation_entry __user *)
				   (uintptr_t) args->buffers_ptr,
				   exec2_list,
				   sizeof(*exec2_list) * args->buffer_count);
		if (ret) {
			ret = -EFAULT;
			DRM_ERROR("failed to copy %d exec entries "
				  "back to user (%d)\n",
				  args->buffer_count, ret);
		}
	}

	drm_free_large(exec2_list);
	return ret;
}

int
i915_gem_object_pin(struct drm_i915_gem_object *obj,
		    uint32_t alignment,
		    bool map_and_fenceable)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	BUG_ON(obj->pin_count == DRM_I915_GEM_OBJECT_MAX_PIN_COUNT);
	WARN_ON(i915_verify_lists(dev));

	if (obj->gtt_space != NULL) {
		if ((alignment && obj->gtt_offset & (alignment - 1)) ||
		    (map_and_fenceable && !obj->map_and_fenceable)) {
			WARN(obj->pin_count,
			     "bo is already pinned with incorrect alignment:"
			     " offset=%x, req.alignment=%x, req.map_and_fenceable=%d,"
			     " obj->map_and_fenceable=%d\n",
			     obj->gtt_offset, alignment,
			     map_and_fenceable,
			     obj->map_and_fenceable);
			ret = i915_gem_object_unbind(obj);
			if (ret)
				return ret;
		}
	}

	if (obj->gtt_space == NULL) {
		ret = i915_gem_object_bind_to_gtt(obj, alignment,
						  map_and_fenceable);
		if (ret)
			return ret;
	}

	if (obj->pin_count++ == 0) {
		i915_gem_info_add_pin(dev_priv, obj, map_and_fenceable);
		if (!obj->active)
			list_move_tail(&obj->mm_list,
				       &dev_priv->mm.pinned_list);
	}
	BUG_ON(!obj->pin_mappable && map_and_fenceable);

	WARN_ON(i915_verify_lists(dev));
	return 0;
}

void
i915_gem_object_unpin(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	WARN_ON(i915_verify_lists(dev));
	BUG_ON(obj->pin_count == 0);
	BUG_ON(obj->gtt_space == NULL);

	if (--obj->pin_count == 0) {
		if (!obj->active)
			list_move_tail(&obj->mm_list,
				       &dev_priv->mm.inactive_list);
		i915_gem_info_remove_pin(dev_priv, obj);
	}
	WARN_ON(i915_verify_lists(dev));
}

int
i915_gem_pin_ioctl(struct drm_device *dev, void *data,
		   struct drm_file *file)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to pin a purgeable buffer\n");
		ret = -EINVAL;
		goto out;
	}

	if (obj->pin_filp != NULL && obj->pin_filp != file) {
		DRM_ERROR("Already pinned in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		ret = -EINVAL;
		goto out;
	}

	obj->user_pin_count++;
	obj->pin_filp = file;
	if (obj->user_pin_count == 1) {
		ret = i915_gem_object_pin(obj, args->alignment, true);
		if (ret)
			goto out;
	}

	/* XXX - flush the CPU caches for pinned objects
	 * as the X server doesn't manage domains yet
	 */
	i915_gem_object_flush_cpu_write_domain(obj);
	args->offset = obj->gtt_offset;
out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_unpin_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->pin_filp != file) {
		DRM_ERROR("Not pinned by caller in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		ret = -EINVAL;
		goto out;
	}
	obj->user_pin_count--;
	if (obj->user_pin_count == 0) {
		obj->pin_filp = NULL;
		i915_gem_object_unpin(obj);
	}

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_busy_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Count all active objects as busy, even if they are currently not used
	 * by the gpu. Users of this interface expect objects to eventually
	 * become non-busy without any further actions, therefore emit any
	 * necessary flushes here.
	 */
	args->busy = obj->active;
	if (args->busy) {
		/* Unconditionally flush objects, even when the gpu still uses this
		 * object. Userspace calling this function indicates that it wants to
		 * use this buffer rather sooner than later, so issuing the required
		 * flush earlier is beneficial.
		 */
		if (obj->base.write_domain & I915_GEM_GPU_DOMAINS)
			i915_gem_flush_ring(dev, obj->ring,
					    0, obj->base.write_domain);

		/* Update the active list for the hardware's current position.
		 * Otherwise this only updates on a delayed timer or when irqs
		 * are actually unmasked, and our working set ends up being
		 * larger than required.
		 */
		i915_gem_retire_requests_ring(dev, obj->ring);

		args->busy = obj->active;
	}

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
    return i915_gem_ring_throttle(dev, file_priv);
}

int
i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_i915_gem_madvise *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	switch (args->madv) {
	case I915_MADV_DONTNEED:
	case I915_MADV_WILLNEED:
	    break;
	default:
	    return -EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file_priv, args->handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->pin_count) {
		ret = -EINVAL;
		goto out;
	}

	if (obj->madv != __I915_MADV_PURGED)
		obj->madv = args->madv;

	/* if the object is no longer bound, discard its backing storage */
	if (i915_gem_object_is_purgeable(obj) &&
	    obj->gtt_space == NULL)
		i915_gem_object_truncate(obj);

	args->retained = obj->madv != __I915_MADV_PURGED;

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

struct drm_i915_gem_object *i915_gem_alloc_object(struct drm_device *dev,
						  size_t size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->base, size) != 0) {
		kfree(obj);
		return NULL;
	}

	i915_gem_info_add_obj(dev_priv, size);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	obj->agp_type = AGP_USER_MEMORY;
	obj->base.driver_private = NULL;
	obj->fence_reg = I915_FENCE_REG_NONE;
	INIT_LIST_HEAD(&obj->mm_list);
	INIT_LIST_HEAD(&obj->gtt_list);
	INIT_LIST_HEAD(&obj->ring_list);
	INIT_LIST_HEAD(&obj->gpu_write_list);
	obj->madv = I915_MADV_WILLNEED;
	/* Avoid an unnecessary call to unbind on the first bind. */
	obj->map_and_fenceable = true;

	return obj;
}

int i915_gem_init_object(struct drm_gem_object *obj)
{
	BUG();

	return 0;
}

static void i915_gem_free_object_tail(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_object_unbind(obj);
	if (ret == -ERESTARTSYS) {
		list_move(&obj->mm_list,
			  &dev_priv->mm.deferred_free_list);
		return;
	}

	if (obj->base.map_list.map)
		i915_gem_free_mmap_offset(obj);

	drm_gem_object_release(&obj->base);
	i915_gem_info_remove_obj(dev_priv, obj->base.size);

	kfree(obj->page_cpu_valid);
	kfree(obj->bit_17);
	kfree(obj);
}

void i915_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_device *dev = obj->base.dev;

	trace_i915_gem_object_destroy(obj);

	while (obj->pin_count > 0)
		i915_gem_object_unpin(obj);

	if (obj->phys_obj)
		i915_gem_detach_phys_object(dev, obj);

	i915_gem_free_object_tail(obj);
}

int
i915_gem_idle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	mutex_lock(&dev->struct_mutex);

	if (dev_priv->mm.suspended) {
		mutex_unlock(&dev->struct_mutex);
		return 0;
	}

	ret = i915_gpu_idle(dev);
	if (ret) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* Under UMS, be paranoid and evict. */
	if (!drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = i915_gem_evict_inactive(dev, false);
		if (ret) {
			mutex_unlock(&dev->struct_mutex);
			return ret;
		}
	}

	/* Hack!  Don't let anybody do execbuf while we don't control the chip.
	 * We need to replace this with a semaphore, or something.
	 * And not confound mm.suspended!
	 */
	dev_priv->mm.suspended = 1;
	del_timer_sync(&dev_priv->hangcheck_timer);

	i915_kernel_lost_context(dev);
	i915_gem_cleanup_ringbuffer(dev);

	mutex_unlock(&dev->struct_mutex);

	/* Cancel the retire work handler, which should be idle now. */
	cancel_delayed_work_sync(&dev_priv->mm.retire_work);

	return 0;
}

int
i915_gem_init_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	ret = intel_init_render_ring_buffer(dev);
	if (ret)
		return ret;

	if (HAS_BSD(dev)) {
		ret = intel_init_bsd_ring_buffer(dev);
		if (ret)
			goto cleanup_render_ring;
	}

	if (HAS_BLT(dev)) {
		ret = intel_init_blt_ring_buffer(dev);
		if (ret)
			goto cleanup_bsd_ring;
	}

	dev_priv->next_seqno = 1;

	return 0;

cleanup_bsd_ring:
	intel_cleanup_ring_buffer(&dev_priv->bsd_ring);
cleanup_render_ring:
	intel_cleanup_ring_buffer(&dev_priv->render_ring);
	return ret;
}

void
i915_gem_cleanup_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	intel_cleanup_ring_buffer(&dev_priv->render_ring);
	intel_cleanup_ring_buffer(&dev_priv->bsd_ring);
	intel_cleanup_ring_buffer(&dev_priv->blt_ring);
}

int
i915_gem_entervt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	if (atomic_read(&dev_priv->mm.wedged)) {
		DRM_ERROR("Reenabling wedged hardware, good luck\n");
		atomic_set(&dev_priv->mm.wedged, 0);
	}

	mutex_lock(&dev->struct_mutex);
	dev_priv->mm.suspended = 0;

	ret = i915_gem_init_ringbuffer(dev);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	BUG_ON(!list_empty(&dev_priv->mm.active_list));
	BUG_ON(!list_empty(&dev_priv->render_ring.active_list));
	BUG_ON(!list_empty(&dev_priv->bsd_ring.active_list));
	BUG_ON(!list_empty(&dev_priv->blt_ring.active_list));
	BUG_ON(!list_empty(&dev_priv->mm.flushing_list));
	BUG_ON(!list_empty(&dev_priv->mm.inactive_list));
	BUG_ON(!list_empty(&dev_priv->render_ring.request_list));
	BUG_ON(!list_empty(&dev_priv->bsd_ring.request_list));
	BUG_ON(!list_empty(&dev_priv->blt_ring.request_list));
	mutex_unlock(&dev->struct_mutex);

	ret = drm_irq_install(dev);
	if (ret)
		goto cleanup_ringbuffer;

	return 0;

cleanup_ringbuffer:
	mutex_lock(&dev->struct_mutex);
	i915_gem_cleanup_ringbuffer(dev);
	dev_priv->mm.suspended = 1;
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
i915_gem_leavevt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	drm_irq_uninstall(dev);
	return i915_gem_idle(dev);
}

void
i915_gem_lastclose(struct drm_device *dev)
{
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ret = i915_gem_idle(dev);
	if (ret)
		DRM_ERROR("failed to idle hardware: %d\n", ret);
}

static void
init_ring_lists(struct intel_ring_buffer *ring)
{
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	INIT_LIST_HEAD(&ring->gpu_write_list);
}

void
i915_gem_load(struct drm_device *dev)
{
	int i;
	drm_i915_private_t *dev_priv = dev->dev_private;

	INIT_LIST_HEAD(&dev_priv->mm.active_list);
	INIT_LIST_HEAD(&dev_priv->mm.flushing_list);
	INIT_LIST_HEAD(&dev_priv->mm.inactive_list);
	INIT_LIST_HEAD(&dev_priv->mm.pinned_list);
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	INIT_LIST_HEAD(&dev_priv->mm.deferred_free_list);
	INIT_LIST_HEAD(&dev_priv->mm.gtt_list);
	init_ring_lists(&dev_priv->render_ring);
	init_ring_lists(&dev_priv->bsd_ring);
	init_ring_lists(&dev_priv->blt_ring);
	for (i = 0; i < 16; i++)
		INIT_LIST_HEAD(&dev_priv->fence_regs[i].lru_list);
	INIT_DELAYED_WORK(&dev_priv->mm.retire_work,
			  i915_gem_retire_work_handler);
	init_completion(&dev_priv->error_completion);

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	if (IS_GEN3(dev)) {
		u32 tmp = I915_READ(MI_ARB_STATE);
		if (!(tmp & MI_ARB_C3_LP_WRITE_ENABLE)) {
			/* arb state is a masked write, so set bit + bit in mask */
			tmp = MI_ARB_C3_LP_WRITE_ENABLE | (MI_ARB_C3_LP_WRITE_ENABLE << MI_ARB_MASK_SHIFT);
			I915_WRITE(MI_ARB_STATE, tmp);
		}
	}

	/* Old X drivers will take 0-2 for front, back, depth buffers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev_priv->fence_reg_start = 3;

	if (INTEL_INFO(dev)->gen >= 4 || IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
		dev_priv->num_fence_regs = 16;
	else
		dev_priv->num_fence_regs = 8;

	/* Initialize fence registers to zero */
	switch (INTEL_INFO(dev)->gen) {
	case 6:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_SANDYBRIDGE_0 + (i * 8), 0);
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_965_0 + (i * 8), 0);
		break;
	case 3:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				I915_WRITE(FENCE_REG_945_8 + (i * 4), 0);
	case 2:
		for (i = 0; i < 8; i++)
			I915_WRITE(FENCE_REG_830_0 + (i * 4), 0);
		break;
	}
	i915_gem_detect_bit_6_swizzle(dev);
	init_waitqueue_head(&dev_priv->pending_flip_queue);

	dev_priv->mm.inactive_shrinker.shrink = i915_gem_inactive_shrink;
	dev_priv->mm.inactive_shrinker.seeks = DEFAULT_SEEKS;
	register_shrinker(&dev_priv->mm.inactive_shrinker);
}

/*
 * Create a physically contiguous memory object for this object
 * e.g. for cursor + overlay regs
 */
static int i915_gem_init_phys_object(struct drm_device *dev,
				     int id, int size, int align)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;
	int ret;

	if (dev_priv->mm.phys_objs[id - 1] || !size)
		return 0;

	phys_obj = kzalloc(sizeof(struct drm_i915_gem_phys_object), GFP_KERNEL);
	if (!phys_obj)
		return -ENOMEM;

	phys_obj->id = id;

	phys_obj->handle = drm_pci_alloc(dev, size, align);
	if (!phys_obj->handle) {
		ret = -ENOMEM;
		goto kfree_obj;
	}
#ifdef CONFIG_X86
	set_memory_wc((unsigned long)phys_obj->handle->vaddr, phys_obj->handle->size / PAGE_SIZE);
#endif

	dev_priv->mm.phys_objs[id - 1] = phys_obj;

	return 0;
kfree_obj:
	kfree(phys_obj);
	return ret;
}

static void i915_gem_free_phys_object(struct drm_device *dev, int id)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;

	if (!dev_priv->mm.phys_objs[id - 1])
		return;

	phys_obj = dev_priv->mm.phys_objs[id - 1];
	if (phys_obj->cur_obj) {
		i915_gem_detach_phys_object(dev, phys_obj->cur_obj);
	}

#ifdef CONFIG_X86
	set_memory_wb((unsigned long)phys_obj->handle->vaddr, phys_obj->handle->size / PAGE_SIZE);
#endif
	drm_pci_free(dev, phys_obj->handle);
	kfree(phys_obj);
	dev_priv->mm.phys_objs[id - 1] = NULL;
}

void i915_gem_free_all_phys_object(struct drm_device *dev)
{
	int i;

	for (i = I915_GEM_PHYS_CURSOR_0; i <= I915_MAX_PHYS_OBJECT; i++)
		i915_gem_free_phys_object(dev, i);
}

void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_i915_gem_object *obj)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	char *vaddr;
	int i;
	int page_count;

	if (!obj->phys_obj)
		return;
	vaddr = obj->phys_obj->handle->vaddr;

	page_count = obj->base.size / PAGE_SIZE;
	for (i = 0; i < page_count; i++) {
		struct page *page = read_cache_page_gfp(mapping, i,
							GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (!IS_ERR(page)) {
			char *dst = kmap_atomic(page);
			memcpy(dst, vaddr + i*PAGE_SIZE, PAGE_SIZE);
			kunmap_atomic(dst);

			drm_clflush_pages(&page, 1);

			set_page_dirty(page);
			mark_page_accessed(page);
			page_cache_release(page);
		}
	}
	intel_gtt_chipset_flush();

	obj->phys_obj->cur_obj = NULL;
	obj->phys_obj = NULL;
}

int
i915_gem_attach_phys_object(struct drm_device *dev,
			    struct drm_i915_gem_object *obj,
			    int id,
			    int align)
{
	struct address_space *mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = 0;
	int page_count;
	int i;

	if (id > I915_MAX_PHYS_OBJECT)
		return -EINVAL;

	if (obj->phys_obj) {
		if (obj->phys_obj->id == id)
			return 0;
		i915_gem_detach_phys_object(dev, obj);
	}

	/* create a new object */
	if (!dev_priv->mm.phys_objs[id - 1]) {
		ret = i915_gem_init_phys_object(dev, id,
						obj->base.size, align);
		if (ret) {
			DRM_ERROR("failed to init phys object %d size: %zu\n",
				  id, obj->base.size);
			return ret;
		}
	}

	/* bind to the object */
	obj->phys_obj = dev_priv->mm.phys_objs[id - 1];
	obj->phys_obj->cur_obj = obj;

	page_count = obj->base.size / PAGE_SIZE;

	for (i = 0; i < page_count; i++) {
		struct page *page;
		char *dst, *src;

		page = read_cache_page_gfp(mapping, i,
					   GFP_HIGHUSER | __GFP_RECLAIMABLE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		src = kmap_atomic(page);
		dst = obj->phys_obj->handle->vaddr + (i * PAGE_SIZE);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(src);

		mark_page_accessed(page);
		page_cache_release(page);
	}

	return 0;
}

static int
i915_gem_phys_pwrite(struct drm_device *dev,
		     struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     struct drm_file *file_priv)
{
	void *vaddr = obj->phys_obj->handle->vaddr + args->offset;
	char __user *user_data = (char __user *) (uintptr_t) args->data_ptr;

	if (__copy_from_user_inatomic_nocache(vaddr, user_data, args->size)) {
		unsigned long unwritten;

		/* The physical object once assigned is fixed for the lifetime
		 * of the obj, so we can safely drop the lock and continue
		 * to access vaddr.
		 */
		mutex_unlock(&dev->struct_mutex);
		unwritten = copy_from_user(vaddr, user_data, args->size);
		mutex_lock(&dev->struct_mutex);
		if (unwritten)
			return -EFAULT;
	}

	intel_gtt_chipset_flush();
	return 0;
}

void i915_gem_release(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	/* Clean up our request list when the client is going away, so that
	 * later retire_requests won't dereference our soon-to-be-gone
	 * file_priv.
	 */
	spin_lock(&file_priv->mm.lock);
	while (!list_empty(&file_priv->mm.request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&file_priv->mm.request_list,
					   struct drm_i915_gem_request,
					   client_list);
		list_del(&request->client_list);
		request->file_priv = NULL;
	}
	spin_unlock(&file_priv->mm.lock);
}

static int
i915_gpu_is_active(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int lists_empty;

	lists_empty = list_empty(&dev_priv->mm.flushing_list) &&
		      list_empty(&dev_priv->mm.active_list);

	return !lists_empty;
}

static int
i915_gem_inactive_shrink(struct shrinker *shrinker,
			 int nr_to_scan,
			 gfp_t gfp_mask)
{
	struct drm_i915_private *dev_priv =
		container_of(shrinker,
			     struct drm_i915_private,
			     mm.inactive_shrinker);
	struct drm_device *dev = dev_priv->dev;
	struct drm_i915_gem_object *obj, *next;
	int cnt;

	if (!mutex_trylock(&dev->struct_mutex))
		return 0;

	/* "fast-path" to count number of available objects */
	if (nr_to_scan == 0) {
		cnt = 0;
		list_for_each_entry(obj,
				    &dev_priv->mm.inactive_list,
				    mm_list)
			cnt++;
		mutex_unlock(&dev->struct_mutex);
		return cnt / 100 * sysctl_vfs_cache_pressure;
	}

rescan:
	/* first scan for clean buffers */
	i915_gem_retire_requests(dev);

	list_for_each_entry_safe(obj, next,
				 &dev_priv->mm.inactive_list,
				 mm_list) {
		if (i915_gem_object_is_purgeable(obj)) {
			i915_gem_object_unbind(obj);
			if (--nr_to_scan == 0)
				break;
		}
	}

	/* second pass, evict/count anything still on the inactive list */
	cnt = 0;
	list_for_each_entry_safe(obj, next,
				 &dev_priv->mm.inactive_list,
				 mm_list) {
		if (nr_to_scan) {
			i915_gem_object_unbind(obj);
			nr_to_scan--;
		} else
			cnt++;
	}

	if (nr_to_scan && i915_gpu_is_active(dev)) {
		/*
		 * We are desperate for pages, so as a last resort, wait
		 * for the GPU to finish and discard whatever we can.
		 * This has a dramatic impact to reduce the number of
		 * OOM-killer events whilst running the GPU aggressively.
		 */
		if (i915_gpu_idle(dev) == 0)
			goto rescan;
	}
	mutex_unlock(&dev->struct_mutex);
	return cnt / 100 * sysctl_vfs_cache_pressure;
}
