#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/random.h>
#include <asm/timex.h>

#define NODES       100
#define PERF_LOOPS  100000
#define CHECK_LOOPS 100

struct test_node {
	struct rb_node rb;
	u32 key;

	/* following fields used for testing augmented rbtree functionality */
	u32 val;
	u32 augmented;
};

static struct rb_root root = RB_ROOT;
static struct test_node nodes[NODES];

static struct rnd_state rnd;

static void insert(struct test_node *node, struct rb_root *root)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;
	u32 key = node->key;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_node, rb)->key)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_node(&node->rb, parent, new);
	rb_insert_color(&node->rb, root);
}

static inline void erase(struct test_node *node, struct rb_root *root)
{
	rb_erase(&node->rb, root);
}

static inline u32 augment_recompute(struct test_node *node)
{
	u32 max = node->val, child_augmented;
	if (node->rb.rb_left) {
		child_augmented = rb_entry(node->rb.rb_left, struct test_node,
					   rb)->augmented;
		if (max < child_augmented)
			max = child_augmented;
	}
	if (node->rb.rb_right) {
		child_augmented = rb_entry(node->rb.rb_right, struct test_node,
					   rb)->augmented;
		if (max < child_augmented)
			max = child_augmented;
	}
	return max;
}

static void augment_callback(struct rb_node *rb, void *unused)
{
	struct test_node *node = rb_entry(rb, struct test_node, rb);
	node->augmented = augment_recompute(node);
}

static void insert_augmented(struct test_node *node, struct rb_root *root)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;
	u32 key = node->key;

	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_node, rb)->key)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	rb_link_node(&node->rb, parent, new);
	rb_insert_color(&node->rb, root);
	rb_augment_insert(&node->rb, augment_callback, NULL);
}

static void erase_augmented(struct test_node *node, struct rb_root *root)
{
	struct rb_node *deepest = rb_augment_erase_begin(&node->rb);
	rb_erase(&node->rb, root);
	rb_augment_erase_end(deepest, augment_callback, NULL);
}

static void init(void)
{
	int i;
	for (i = 0; i < NODES; i++) {
		nodes[i].key = prandom32(&rnd);
		nodes[i].val = prandom32(&rnd);
	}
}

static bool is_red(struct rb_node *rb)
{
	return !(rb->__rb_parent_color & 1);
}

static int black_path_count(struct rb_node *rb)
{
	int count;
	for (count = 0; rb; rb = rb_parent(rb))
		count += !is_red(rb);
	return count;
}

static void check(int nr_nodes)
{
	struct rb_node *rb;
	int count = 0;
	int blacks;
	u32 prev_key = 0;

	for (rb = rb_first(&root); rb; rb = rb_next(rb)) {
		struct test_node *node = rb_entry(rb, struct test_node, rb);
		WARN_ON_ONCE(node->key < prev_key);
		WARN_ON_ONCE(is_red(rb) &&
			     (!rb_parent(rb) || is_red(rb_parent(rb))));
		if (!count)
			blacks = black_path_count(rb);
		else
			WARN_ON_ONCE((!rb->rb_left || !rb->rb_right) &&
				     blacks != black_path_count(rb));
		prev_key = node->key;
		count++;
	}
	WARN_ON_ONCE(count != nr_nodes);
}

static void check_augmented(int nr_nodes)
{
	struct rb_node *rb;

	check(nr_nodes);
	for (rb = rb_first(&root); rb; rb = rb_next(rb)) {
		struct test_node *node = rb_entry(rb, struct test_node, rb);
		WARN_ON_ONCE(node->augmented != augment_recompute(node));
	}
}

static int rbtree_test_init(void)
{
	int i, j;
	cycles_t time1, time2, time;

	printk(KERN_ALERT "rbtree testing");

	prandom32_seed(&rnd, 3141592653589793238ULL);
	init();

	time1 = get_cycles();

	for (i = 0; i < PERF_LOOPS; i++) {
		for (j = 0; j < NODES; j++)
			insert(nodes + j, &root);
		for (j = 0; j < NODES; j++)
			erase(nodes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, PERF_LOOPS);
	printk(" -> %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < CHECK_LOOPS; i++) {
		init();
		for (j = 0; j < NODES; j++) {
			check(j);
			insert(nodes + j, &root);
		}
		for (j = 0; j < NODES; j++) {
			check(NODES - j);
			erase(nodes + j, &root);
		}
		check(0);
	}

	printk(KERN_ALERT "augmented rbtree testing");

	init();

	time1 = get_cycles();

	for (i = 0; i < PERF_LOOPS; i++) {
		for (j = 0; j < NODES; j++)
			insert_augmented(nodes + j, &root);
		for (j = 0; j < NODES; j++)
			erase_augmented(nodes + j, &root);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, PERF_LOOPS);
	printk(" -> %llu cycles\n", (unsigned long long)time);

	for (i = 0; i < CHECK_LOOPS; i++) {
		init();
		for (j = 0; j < NODES; j++) {
			check_augmented(j);
			insert_augmented(nodes + j, &root);
		}
		for (j = 0; j < NODES; j++) {
			check_augmented(NODES - j);
			erase_augmented(nodes + j, &root);
		}
		check_augmented(0);
	}

	return -EAGAIN; /* Fail will directly unload the module */
}

static void rbtree_test_exit(void)
{
	printk(KERN_ALERT "test exit\n");
}

module_init(rbtree_test_init)
module_exit(rbtree_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michel Lespinasse");
MODULE_DESCRIPTION("Red Black Tree test");
