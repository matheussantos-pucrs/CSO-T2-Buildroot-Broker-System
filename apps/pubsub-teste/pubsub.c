#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#define TOPIC_NAME_LEN	32
#define MAX_WRITE_LEN	512

/* ---------- parametros do modulo ---------- */
static int max_topics = 8;
module_param(max_topics, int, 0444);
MODULE_PARM_DESC(max_topics, "Numero maximo de topicos");

static int default_max_subs = 4;
module_param(default_max_subs, int, 0444);
MODULE_PARM_DESC(default_max_subs, "Max. inicial de inscritos por topico (ajustavel via /sys/pubsub/<topico>)");

/* ---------- estruturas ---------- */
struct message {
	struct list_head node;		
	size_t len;
	char text[];			
};

struct subscriber {
	struct list_head node;		
	pid_t pid;
	struct list_head msgs;		
};

struct topic {
	struct list_head node;		
	char name[TOPIC_NAME_LEN];
	int nsubs;
	int max_subs;			
	struct list_head subs;
	struct kobj_attribute kattr;	
};

struct session {
	char fetch[TOPIC_NAME_LEN];	
};

static LIST_HEAD(topics);
static int ntopics;
static DEFINE_MUTEX(ps_lock);		
static struct kobject *pubsub_kobj;


static ssize_t max_subs_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct topic *t = container_of(attr, struct topic, kattr);

	return sysfs_emit(buf, "%d\n", READ_ONCE(t->max_subs));
}

static ssize_t max_subs_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	struct topic *t = container_of(attr, struct topic, kattr);
	int v, ret;

	ret = kstrtoint(buf, 10, &v);
	if (ret)
		return ret;
	if (v < 0)
		return -EINVAL;
	/* nao expulsa ninguem se v < nsubs: so impede novas inscricoes */
	WRITE_ONCE(t->max_subs, v);
	return count;
}

/* ---------- helpers (chamar com ps_lock) ---------- */
static struct topic *topic_find(const char *name)
{
	struct topic *t;

	list_for_each_entry(t, &topics, node)
		if (!strcmp(t->name, name))
			return t;
	return NULL;
}

static struct subscriber *sub_find(struct topic *t, pid_t pid)
{
	struct subscriber *s;

	list_for_each_entry(s, &t->subs, node)
		if (s->pid == pid)
			return s;
	return NULL;
}

/* remove o inscrito e libera todas as suas mensagens pendentes */
static void sub_remove(struct topic *t, struct subscriber *s)
{
	struct message *m, *tmp;

	list_for_each_entry_safe(m, tmp, &s->msgs, node) {
		list_del(&m->node);
		kfree(m);
	}
	list_del(&s->node);
	kfree(s);
	t->nsubs--;
}

static void topic_destroy(struct topic *t)
{
	sysfs_remove_file(pubsub_kobj, &t->kattr.attr);
	list_del(&t->node);
	ntopics--;
	kfree(t);
}

/* remove o topico se ficou sem inscritos */
static void topic_gc(struct topic *t)
{
	if (list_empty(&t->subs))
		topic_destroy(t);
}

static struct topic *topic_create(const char *name)
{
	struct topic *t;

	if (ntopics >= max_topics)
		return NULL;		

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	strscpy(t->name, name, sizeof(t->name));
	INIT_LIST_HEAD(&t->subs);
	t->max_subs = default_max_subs;

	sysfs_attr_init(&t->kattr.attr);
	t->kattr.attr.name = t->name;
	t->kattr.attr.mode = 0664;
	t->kattr.show = max_subs_show;
	t->kattr.store = max_subs_store;
	if (sysfs_create_file(pubsub_kobj, &t->kattr.attr)) {
		kfree(t);
		return NULL;
	}

	list_add_tail(&t->node, &topics);
	ntopics++;
	return t;
}

/* ---------- comandos (chamar com ps_lock) ---------- */
static int do_subscribe(pid_t pid, const char *name)
{
	struct topic *t = topic_find(name);
	struct subscriber *s;

	if (!t) {
		t = topic_create(name);
		if (!t)
			return 0;	

	if (sub_find(t, pid))
		return 0;		

	if (t->nsubs >= READ_ONCE(t->max_subs)) {
		topic_gc(t);		
		return 0;		
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		topic_gc(t);
		return -ENOMEM;
	}
	s->pid = pid;
	INIT_LIST_HEAD(&s->msgs);
	list_add_tail(&s->node, &t->subs);
	t->nsubs++;
	return 0;
}

static int do_unsubscribe(pid_t pid, const char *name)
{
	struct topic *t = topic_find(name);
	struct subscriber *s;

	if (!t)
		return 0;
	s = sub_find(t, pid);
	if (!s)
		return 0;
	sub_remove(t, s);
	topic_gc(t);
	return 0;
}

static int do_publish(const char *name, const char *text)
{
	struct topic *t = topic_find(name);
	struct subscriber *s;
	struct message *m;
	size_t len = strlen(text);

	if (!t)
		return 0;		
	list_for_each_entry(s, &t->subs, node) {
		m = kmalloc(sizeof(*m) + len + 1, GFP_KERNEL);
		if (!m)
			return -ENOMEM;
		memcpy(m->text, text, len);
		m->text[len] = '\n';
		m->len = len + 1;
		list_add_tail(&m->node, &s->msgs);
	}
	return 0;
}


static int ps_open(struct inode *inode, struct file *filp)
{
	struct session *ses = kzalloc(sizeof(*ses), GFP_KERNEL);

	if (!ses)
		return -ENOMEM;
	filp->private_data = ses;
	return 0;
}

static int ps_release(struct inode *inode, struct file *filp)
{
	pid_t pid = task_pid_nr(current);
	struct topic *t, *tmp;
	struct subscriber *s;

	mutex_lock(&ps_lock);
	list_for_each_entry_safe(t, tmp, &topics, node) {
		s = sub_find(t, pid);
		if (s) {
			sub_remove(t, s);
			topic_gc(t);	
		}
	}
	mutex_unlock(&ps_lock);

	kfree(filp->private_data);
	return 0;
}

static bool valid_topic(const char *n)
{
	return n && *n && n[0] != '.' && !strchr(n, '/') &&
	       strlen(n) < TOPIC_NAME_LEN;
}

static ssize_t ps_write(struct file *filp, const char __user *ubuf,
			size_t count, loff_t *off)
{
	struct session *ses = filp->private_data;
	pid_t pid = task_pid_nr(current);
	char *buf, *p, *cmd, *topic;
	int ret = -EINVAL;

	if (count == 0 || count > MAX_WRITE_LEN)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	p = strim(buf);			
	cmd = strsep(&p, " ");
	if (!p)
		goto out;		
	p = skip_spaces(p);

	if (!strcmp(cmd, "/publish")) {
		size_t n;

		topic = strsep(&p, " ");
		if (!p || !valid_topic(topic))
			goto out;
		p = skip_spaces(p);
		n = strlen(p);
		if (n >= 2 && p[0] == '"' && p[n - 1] == '"') {	
			p[n - 1] = '\0';
			p++;
		}
		mutex_lock(&ps_lock);
		ret = do_publish(topic, p);
		mutex_unlock(&ps_lock);
	} else if (!strcmp(cmd, "/subscribe")) {
		topic = strsep(&p, " ");
		if (!valid_topic(topic))
			goto out;
		mutex_lock(&ps_lock);
		ret = do_subscribe(pid, topic);
		mutex_unlock(&ps_lock);
	} else if (!strcmp(cmd, "/unsubscribe")) {
		topic = strsep(&p, " ");
		if (!valid_topic(topic))
			goto out;
		mutex_lock(&ps_lock);
		ret = do_unsubscribe(pid, topic);
		mutex_unlock(&ps_lock);
	} else if (!strcmp(cmd, "/fetch")) {
		topic = strsep(&p, " ");
		if (!valid_topic(topic))
			goto out;
		strscpy(ses->fetch, topic, sizeof(ses->fetch));
		ret = 0;
	}
out:
	kfree(buf);
	return ret ? ret : count;
}

/* cada read entrega (e remove) UMA mensagem; 0 = nada pendente */
static ssize_t ps_read(struct file *filp, char __user *ubuf,
		       size_t count, loff_t *off)
{
	struct session *ses = filp->private_data;
	pid_t pid = task_pid_nr(current);
	struct message *m = NULL;
	struct topic *t;
	struct subscriber *s;
	size_t n;

	mutex_lock(&ps_lock);
	t = topic_find(ses->fetch);
	if (t) {
		s = sub_find(t, pid);
		if (s && !list_empty(&s->msgs)) {
			m = list_first_entry(&s->msgs, struct message, node);
			list_del(&m->node);
		}
	}
	mutex_unlock(&ps_lock);

	if (!m)
		return 0;

	n = min(count, m->len);
	if (copy_to_user(ubuf, m->text, n)) {
		kfree(m);
		return -EFAULT;
	}
	kfree(m);
	return n;
}

static const struct file_operations ps_fops = {
	.owner   = THIS_MODULE,
	.open    = ps_open,
	.release = ps_release,
	.read    = ps_read,
	.write   = ps_write,
	.llseek  = noop_llseek,		
};

static struct miscdevice ps_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pubsub",		
	.fops  = &ps_fops,
};

static int __init pubsub_init(void)
{
	int ret;

	pubsub_kobj = kobject_create_and_add("pubsub", NULL);
	if (!pubsub_kobj)
		return -ENOMEM;

	ret = misc_register(&ps_misc);
	if (ret) {
		kobject_put(pubsub_kobj);
		return ret;
	}
	pr_info("pubsub: carregado (max_topics=%d)\n", max_topics);
	return 0;
}

static void __exit pubsub_exit(void)
{
	struct topic *t, *tt;
	struct subscriber *s, *st;

	misc_deregister(&ps_misc);

	mutex_lock(&ps_lock);
	list_for_each_entry_safe(t, tt, &topics, node) {
		list_for_each_entry_safe(s, st, &t->subs, node)
			sub_remove(t, s);
		topic_destroy(t);
	}
	mutex_unlock(&ps_lock);

	kobject_put(pubsub_kobj);
	pr_info("pubsub: descarregado\n");
}

module_init(pubsub_init);
module_exit(pubsub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broker Publish/Subscribe - T2");
