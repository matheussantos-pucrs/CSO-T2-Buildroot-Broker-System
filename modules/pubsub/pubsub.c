
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mutex.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudantes");
MODULE_DESCRIPTION("Publish/Subscribe Character Device Driver");
MODULE_VERSION("0.0.1");

#define DEVICE_NAME "pubsub"
#define DEVCOUNT 1
#define BUFFER_SIZE 256

static dev_t devno = 0;

static ssize_t pubsub_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t pubsub_write(struct file *, const char __user *, size_t, loff_t *);
static int pubsub_open(struct inode *, struct file *);
static int pubsub_release(struct inode *, struct file *);
static int pubsub_subscribe(const char *);
static int pubsub_publish(const char *, const char *);
static int pubsub_fetch(struct file *, const char *);
static int pubsub_unsubscribe(struct file *, const char *);

static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = pubsub_read,
    .write   = pubsub_write,
    .open    = pubsub_open,
    .release = pubsub_release,
    .llseek  = noop_llseek
};

static struct cdev chardev = {};

static struct class *cls = NULL;
static struct device *dev = NULL;

static int max_topics = 10;
module_param(max_topics, int, 0444);
MODULE_PARM_DESC(max_topics, "Maximum number of topics");

#define TOPIC_NAME_SIZE 32

struct message_node {
    struct list_head list;
    struct subscriber_node *subscriber;
    char *message;
};

struct subscriber_node {
    struct list_head list;
    pid_t pid;
    struct list_head messages;
};

struct topic_node {
    struct list_head list;
    char name[TOPIC_NAME_SIZE];
    struct list_head subscribers;
};

static LIST_HEAD(topic_list);

static int number_of_topics = 0;

static DEFINE_MUTEX(pubsub_mutex);


static int pubsub_init(void)
{
    int err;

    pr_info("Inserting the Publish/Subscribe Device\n");

    if (max_topics <= 0) {
        pr_alert("Module not loaded. Pass a positive integer!\n");
        pr_alert("Example: modprobe pubsub max_topics=10\n");
        return -EINVAL;
    }

    err = alloc_chrdev_region(&devno, 0, DEVCOUNT, DEVICE_NAME);
    if (err != 0) {
        pr_alert("Publish/Subscribe failed to register a major number\n");
        goto err_region;
    }
    pr_info("Publish/Subscribe registered driver and device number(s)\n");

    cdev_init(&chardev, &fops);
    err = cdev_add(&chardev, devno, DEVCOUNT);
    if (err != 0) {
        pr_err("Publish/Subscribe failed to add a device\n");
        goto err_cdev;
    }
    pr_info("Publish/Subscribe successfully added device\n");

    cls = class_create(DEVICE_NAME);
    if (IS_ERR(cls)) {
        pr_alert("Publish/Subscribe failed to register a class\n");
        err = PTR_ERR(cls);
        goto err_cls;
    }
    pr_info("Publish/Subscribe registered class\n");

    dev = device_create(cls, NULL, devno, NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        pr_alert("Publish/Subscribe failed to create devfs file\n");
        err = PTR_ERR(dev);
        goto err_dev;
    }
    pr_info("Publish/Subscribe created devfs file\n");

    pr_info("Publish/Subscribe module loaded successfully\n");
    pr_info("Maximum number of topics: %d\n", max_topics);

    return 0;

err_dev:
    class_destroy(cls);
err_cls:
    cdev_del(&chardev);
err_cdev:
    unregister_chrdev_region(devno, DEVCOUNT);
err_region:
    return err;
}

static void pubsub_exit(void)
{
    struct topic_node *topic;
    struct topic_node *tmp_topic;
    struct subscriber_node *subscriber;
    struct subscriber_node *tmp_subscriber;
    struct message_node *message;
    struct message_node *tmp_message;

    list_for_each_entry_safe(topic, tmp_topic, &topic_list, list) {
        list_for_each_entry_safe(subscriber, tmp_subscriber, &topic->subscribers, list) {
            list_for_each_entry_safe(message, tmp_message, &subscriber->messages, list) {
                list_del(&message->list);
                kfree(message->message);
                kfree(message);
            }
            list_del(&subscriber->list);
            kfree(subscriber);
        }

        list_del(&topic->list);
        kfree(topic);
    }

    number_of_topics = 0;

    device_destroy(cls, devno);
    class_destroy(cls);
    cdev_del(&chardev);
    unregister_chrdev_region(devno, DEVCOUNT);

    pr_info("Publish/Subscribe module unloaded successfully\n");
}

static int pubsub_open(struct inode *inodep, struct file *filep)
{
    filep->private_data = NULL;
    pr_info("Publish/Subscribe device opened by PID %d\n", task_pid_nr(current));

    return 0;
}

static int pubsub_release(struct inode *inodep, struct file *filep)
{
    struct topic_node *topic;
    struct topic_node *tmp_topic;
    struct subscriber_node *subscriber;
    struct subscriber_node *tmp_subscriber;
    struct message_node *message;
    struct message_node *tmp_message;
    pid_t pid = task_pid_nr(current);

    mutex_lock(&pubsub_mutex);

    list_for_each_entry_safe(topic, tmp_topic, &topic_list, list) {
        list_for_each_entry_safe(subscriber, tmp_subscriber, &topic->subscribers, list) {
            if (subscriber->pid == pid) {

                /* Libera todas as mensagens pendentes do processo */
                list_for_each_entry_safe(message, tmp_message, &subscriber->messages, list) {
                    list_del(&message->list);
                    kfree(message->message);
                    kfree(message);
                }

                /* Remove o processo da lista de inscritos */
                list_del(&subscriber->list);
                kfree(subscriber);

                pr_info("PID %d unsubscribed from '%s'\n", pid, topic->name);
            }
        }

        /* Remove o topico caso nao tenha mais inscritos */
        if (list_empty(&topic->subscribers)) {
            pr_info("Topic '%s' removed\n", topic->name);

            list_del(&topic->list);
            kfree(topic);

            number_of_topics--;
        }
    }

    mutex_unlock(&pubsub_mutex);
    kfree(filep->private_data);
    filep->private_data = NULL;

    pr_info("Publish/Subscribe device closed by PID %d\n", pid);

    return 0;
}

static ssize_t pubsub_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    struct topic_node *topic;
    struct subscriber_node *subscriber;
    struct message_node *message;
    char *selected_topic;
    pid_t pid = task_pid_nr(current);
    size_t message_len;
    ssize_t ret = -ENOENT;

    if (len == 0)
        return 0;

    mutex_lock(&pubsub_mutex);

    selected_topic = filep->private_data;

    if (selected_topic == NULL) {
        pr_alert("No topic selected for PID %d\n", pid);
        ret = -EINVAL;
        goto unlock;
    }

    list_for_each_entry(topic, &topic_list, list) {
        if (strcmp(topic->name, selected_topic) == 0) {
            ret = -EACCES;

            list_for_each_entry(subscriber, &topic->subscribers, list) {
                if (subscriber->pid == pid) {

                    /* Verifica se existem mensagens pendentes */
                    if (list_empty(&subscriber->messages)) {
                        ret = 0;
                        goto unlock;
                    }

                    /* Obtem a primeira mensagem da fila */
                    message = list_first_entry(&subscriber->messages, struct message_node, list);
                    message_len = strlen(message->message);

                    if (len < message_len) {
                        ret = -EMSGSIZE;
                        goto unlock;
                    }

                    /* Copia a mensagem para o espaco de usuario */
                    if (copy_to_user(buffer, message->message, message_len)) {
                        ret = -EFAULT;
                        goto unlock;
                    }

                    ret = message_len;

                    pr_info("PID %d read %zu bytes from '%s'\n", pid, message_len, topic->name);

                    /* Remove a mensagem da fila */
                    list_del(&message->list);
                    kfree(message->message);
                    kfree(message);

                    goto unlock;
                }
            }

            goto unlock;
        }
    }

unlock:
    mutex_unlock(&pubsub_mutex);

    return ret;
}

static int pubsub_publish(const char *name, const char *text)
{
    struct topic_node *topic;
    struct subscriber_node *subscriber;
    struct message_node *new_message;
    struct message_node *message;
    struct message_node *tmp;
    LIST_HEAD(pending_messages);
    int count = 0;
    int err = 0;
    bool found = false;

    mutex_lock(&pubsub_mutex);

    /* Procura o topico */
    list_for_each_entry(topic, &topic_list, list) {
        if (strcmp(topic->name, name) == 0) {
            found = true;
            break;
        }
    }

    /* Ignora mensagens para topicos inexistentes */
    if (!found)
        goto unlock;

    /* Prepara uma copia para cada inscrito */
    list_for_each_entry(subscriber, &topic->subscribers, list) {
        new_message = kmalloc(sizeof(struct message_node), GFP_KERNEL);

        if (new_message == NULL) {
            err = -ENOMEM;
            goto err_messages;
        }

        new_message->message = kmalloc(strlen(text) + 1, GFP_KERNEL);

        if (new_message->message == NULL) {
            kfree(new_message);
            err = -ENOMEM;
            goto err_messages;
        }

        strcpy(new_message->message, text);
        new_message->subscriber = subscriber;

        list_add_tail(&new_message->list, &pending_messages);
        count++;
    }

    /* Distribui as mensagens para as filas */
    list_for_each_entry_safe(message, tmp, &pending_messages, list) {
        list_move_tail(&message->list, &message->subscriber->messages);
    }

    pr_info("Published message to topic '%s' for %d subscriber(s)\n", name, count);

    goto unlock;

err_messages:
    /* Libera as copias caso ocorra algum erro */
    list_for_each_entry_safe(message, tmp, &pending_messages, list) {
        list_del(&message->list);
        kfree(message->message);
        kfree(message);
    }

unlock:
    mutex_unlock(&pubsub_mutex);

    return err;
}


static int pubsub_fetch(struct file *filep, const char *name)
{
    struct topic_node *topic;
    struct subscriber_node *subscriber;
    char *selected_topic;
    pid_t pid = task_pid_nr(current);
    int err = -ENOENT;

    if (name[0] == '\0' || strchr(name, '/') || strpbrk(name, " \t\r\n"))
        return -EINVAL;

    if (strlen(name) >= TOPIC_NAME_SIZE)
        return -ENAMETOOLONG;

    mutex_lock(&pubsub_mutex);

    list_for_each_entry(topic, &topic_list, list) {
        if (strcmp(topic->name, name) == 0) {
            err = -EACCES;

            list_for_each_entry(subscriber, &topic->subscribers, list) {
                if (subscriber->pid == pid) {
                    selected_topic = kstrdup(name, GFP_KERNEL);

                    if (selected_topic == NULL) {
                        err = -ENOMEM;
                        goto unlock;
                    }

                    kfree(filep->private_data);
                    filep->private_data = selected_topic;

                    pr_info("PID %d selected topic '%s'\n", pid, name);

                    err = 0;
                    goto unlock;
                }
            }

            goto unlock;
        }
    }

unlock:
    mutex_unlock(&pubsub_mutex);

    return err;
}


static int pubsub_unsubscribe(struct file *filep, const char *name)
{
    struct topic_node *topic;
    struct topic_node *tmp_topic;
    struct subscriber_node *subscriber;
    struct subscriber_node *tmp_subscriber;
    struct message_node *message;
    struct message_node *tmp_message;
    pid_t pid = task_pid_nr(current);

    if (name[0] == '\0' || strchr(name, '/') || strpbrk(name, " \t\r\n"))
        return -EINVAL;

    if (strlen(name) >= TOPIC_NAME_SIZE)
        return -ENAMETOOLONG;

    mutex_lock(&pubsub_mutex);

    /* Procura o topico */
    list_for_each_entry_safe(topic, tmp_topic, &topic_list, list) {
        if (strcmp(topic->name, name) != 0)
            continue;

        /* Procura o processo inscrito */
        list_for_each_entry_safe(subscriber, tmp_subscriber, &topic->subscribers, list) {
            if (subscriber->pid != pid)
                continue;

            /* Remove as mensagens pendentes */
            list_for_each_entry_safe(message, tmp_message, &subscriber->messages, list) {
                list_del(&message->list);
                kfree(message->message);
                kfree(message);
            }

            /* Remove a inscricao */
            list_del(&subscriber->list);
            kfree(subscriber);

            pr_info("PID %d unsubscribed from '%s'\n", pid, name);

            /* Limpa o topico selecionado pelo fetch */
            if (filep->private_data != NULL && strcmp(filep->private_data, name) == 0) {
                kfree(filep->private_data);
                filep->private_data = NULL;
            }

            /* Remove o topico se nao houver inscritos */
            if (list_empty(&topic->subscribers)) {
                pr_info("Topic '%s' removed\n", topic->name);

                list_del(&topic->list);
                kfree(topic);

                number_of_topics--;
            }

            goto unlock;
        }

        break;
    }

unlock:
    mutex_unlock(&pubsub_mutex);

    return 0;
}

static ssize_t pubsub_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    char command[BUFFER_SIZE];
    int err;
    char *topic_name;
    char *message_text;
    size_t message_length;

    if (len == 0)
        return 0;

    if (len >= BUFFER_SIZE) {
        pr_alert("Too many characters to deal with (%zu)\n", len);
        return -EMSGSIZE;
    }

    if (copy_from_user(command, buffer, len)) {
        pr_alert("Failed to copy data from user\n");
        return -EFAULT;
    }

    command[len] = '\0';

    pr_info("Received command: %s\n", command);

    if (strncmp(command, "/subscribe ", 11) == 0) {
        err = pubsub_subscribe(strim(command + 11));

        if (err != 0)
            return err;

        return len;
    }

    if (strncmp(command, "/publish ", 9) == 0) {
        topic_name = command + 9;

        message_text = strchr(topic_name, ' ');

        if (message_text == NULL)
            return -EINVAL;

        *message_text = '\0';
        message_text = strim(message_text + 1);

        message_length = strlen(message_text);

        if (message_length < 2 || message_text[0] != '"' || message_text[message_length - 1] != '"')
            return -EINVAL;

        message_text[message_length - 1] = '\0';

        err = pubsub_publish(topic_name, message_text + 1);

        if (err != 0)
            return err;

        return len;
    }

    if (strncmp(command, "/fetch ", 7) == 0) {
        err = pubsub_fetch(filep, strim(command + 7));

        if (err != 0)
            return err;

        return len;
    }

    if (strncmp(command, "/unsubscribe ", 13) == 0) {
        err = pubsub_unsubscribe(filep, strim(command + 13));

        if (err != 0)
            return err;

        return len;
    }

    pr_alert("Unknown command\n");

    return -EINVAL;
}

static int pubsub_subscribe(const char *name)
{
    struct topic_node *topic;
    struct topic_node *new_topic = NULL;
    struct subscriber_node *subscriber;
    struct subscriber_node *new_subscriber;
    pid_t pid = task_pid_nr(current);
    int err = 0;

    if (name[0] == '\0' || strchr(name, '/') || strpbrk(name, " \t\r\n"))
        return -EINVAL;

    if (strlen(name) >= TOPIC_NAME_SIZE)
        return -ENAMETOOLONG;

    mutex_lock(&pubsub_mutex);

    /* Procura o topico */
    list_for_each_entry(topic, &topic_list, list) {
        if (strcmp(topic->name, name) == 0)
            goto topic_found;
    }

    /* Verifica o limite de topicos */
    if (number_of_topics >= max_topics) {
        pr_alert("Maximum number of topics reached\n");
        err = -ENOSPC;
        goto unlock;
    }

    /* Cria o topico */
    new_topic = kmalloc(sizeof(struct topic_node), GFP_KERNEL);
    if (new_topic == NULL) {
        err = -ENOMEM;
        goto unlock;
    }

    strscpy(new_topic->name, name, TOPIC_NAME_SIZE);
    INIT_LIST_HEAD(&new_topic->subscribers);

    list_add_tail(&new_topic->list, &topic_list);
    number_of_topics++;

    topic = new_topic;

    pr_info("Topic '%s' created\n", name);

topic_found:
    /* Verifica se o PID ja esta inscrito */
    list_for_each_entry(subscriber, &topic->subscribers, list) {
        if (subscriber->pid == pid) {
            pr_info("PID %d already subscribed to '%s'\n", pid, name);
            goto unlock;
        }
    }

    /* Cria uma nova inscricao */
    new_subscriber = kmalloc(sizeof(struct subscriber_node), GFP_KERNEL);
    if (new_subscriber == NULL) {
        err = -ENOMEM;
        goto err_subscriber;
    }

    new_subscriber->pid = pid;
    INIT_LIST_HEAD(&new_subscriber->messages);

    list_add_tail(&new_subscriber->list, &topic->subscribers);

    pr_info("PID %d subscribed to '%s'\n", pid, name);

    goto unlock;

err_subscriber:
    if (new_topic != NULL) {
        list_del(&new_topic->list);
        kfree(new_topic);
        number_of_topics--;
    }

unlock:
    mutex_unlock(&pubsub_mutex);

    return err;
}


module_init(pubsub_init);
module_exit(pubsub_exit);
