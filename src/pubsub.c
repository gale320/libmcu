#include "pubsub.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include "list.h"
#include "logger.h"

typedef struct {
	char name[PUBSUB_TOPIC_NAME_MAXLEN];
	struct list pubsub_node; // list entry for the pubsub_list
	pthread_mutex_t subscription_lock;
	struct list subscriptions; // list head for subscriptions
} topic_t;

struct subscribe {
	topic_t *topic;
	struct list subscription_node;
	pubsub_callback_t callback;
	void *context;
};

static DEFINE_LIST_HEAD(pubsub_list);
static pthread_mutex_t pubsub_list_lock;
static volatile bool initialized;

static inline void add_topic(topic_t *p)
{
	pthread_mutex_lock(&pubsub_list_lock);
	{
		list_add(&p->pubsub_node, &pubsub_list);
	}
	pthread_mutex_unlock(&pubsub_list_lock);
}

static inline void remove_topic(topic_t *p)
{
	pthread_mutex_lock(&pubsub_list_lock);
	{
		list_del(&p->pubsub_node, &pubsub_list);
	}
	pthread_mutex_unlock(&pubsub_list_lock);
}

static inline void initialize_subscriptions(topic_t *p)
{
	list_init(&p->subscriptions);
	pthread_mutex_init(&p->subscription_lock, NULL);
}

static inline void remove_subscriptions(topic_t *p)
{
	pubsub_publish(p->name, "topic destroyed", 15);

	pthread_mutex_lock(&p->subscription_lock);
	{
		struct list *i, *j;
		list_for_each_safe(i, j, &p->subscriptions) {
			struct subscribe *sub =
				list_entry(i, struct subscribe, subscription_node);
			list_del(&sub->subscription_node, &p->subscriptions);
			free(sub);
		}
	}
	pthread_mutex_unlock(&p->subscription_lock);
}

static inline int count_subscribers(topic_t *p)
{
	int count = 0;

	pthread_mutex_lock(&p->subscription_lock);
	{
		struct list *i;
		list_for_each(i, &p->subscriptions) {
			count++;
		}
	}
	pthread_mutex_unlock(&p->subscription_lock);

	return count;
}

static topic_t *find_topic(const char * const topic)
{
	topic_t *p = NULL;

	pthread_mutex_lock(&pubsub_list_lock);
	{
		struct list *i;
		list_for_each(i, &pubsub_list) {
			p = list_entry(i, topic_t, pubsub_node);
			if (strcmp(topic, p->name) == 0) {
				break;
			} else {
				p = NULL;
			}
		}
	}
	pthread_mutex_unlock(&pubsub_list_lock);

	return p;
}

pubsub_error_t pubsub_create(const char * const topic)
{
	topic_t *p;

	if (!initialized) {
		pthread_mutex_init(&pubsub_list_lock, NULL);
		initialized = true;
	}

	if (!topic) {
		return PUBSUB_INVALID_PARAM;
	}
	if (find_topic(topic)) {
		return PUBSUB_TOPIC_EXIST;
	}
	if (!(p = (topic_t *)calloc(1, sizeof(*p)))) {
		return PUBSUB_NO_MEMORY;
	}

	strncpy(p->name, topic, PUBSUB_TOPIC_NAME_MAXLEN);
	initialize_subscriptions(p);
	add_topic(p);

	debug("%s topic created", topic);

	return PUBSUB_SUCCESS;
}

pubsub_error_t pubsub_destroy(const char * const topic)
{
	topic_t *p;

	if (!topic) {
		return PUBSUB_INVALID_PARAM;
	}
	if (!(p = find_topic(topic))) {
		return PUBSUB_TOPIC_NOT_EXIST;
	}

	remove_subscriptions(p);
	remove_topic(p);
	free(p);

	debug("%s topic destroyed", topic);

	return PUBSUB_SUCCESS;
}

subscribe_t *pubsub_subscribe(const char * const topic,
		pubsub_callback_t cb, void *context)
{
	topic_t *p;
	struct subscribe *sub;

	if (!topic || !cb || !(p = find_topic(topic)) ||
			!(sub = (struct subscribe *)calloc(1, sizeof(*sub)))) {
		return NULL;
	}

	sub->topic = p;
	sub->callback = cb;
	sub->context = context;

	pthread_mutex_lock(&p->subscription_lock);
	{
		list_add(&sub->subscription_node, &p->subscriptions);
	}
	pthread_mutex_unlock(&p->subscription_lock);

	return sub;
}

pubsub_error_t pubsub_unsubscribe(subscribe_t *handle)
{
	struct subscribe *sub = (struct subscribe *)handle;

	if (!sub || !sub->topic) {
		return PUBSUB_INVALID_PARAM;
	}

	pthread_mutex_lock(&sub->topic->subscription_lock);
	{
		list_del(&sub->subscription_node, &sub->topic->subscriptions);
	}
	pthread_mutex_unlock(&sub->topic->subscription_lock);

	free(sub);

	return PUBSUB_SUCCESS;
}

pubsub_error_t pubsub_publish(const char * const topic,
		const void * const msg, size_t msglen)
{
	topic_t *p;

	if (!topic || !msg || !msglen) {
		return PUBSUB_INVALID_PARAM;
	}
	if (!(p = find_topic(topic))) {
		return PUBSUB_TOPIC_NOT_EXIST;
	}

	pthread_mutex_lock(&p->subscription_lock);
	{
		struct list *i;
		list_for_each(i, &p->subscriptions) {
			struct subscribe *sub =
				list_entry(i, struct subscribe, subscription_node);
			sub->callback(sub->context, msg, msglen);
		}
	}
	pthread_mutex_unlock(&p->subscription_lock);

	return PUBSUB_SUCCESS;
}

int pubsub_count(const char * const topic)
{
	topic_t *p;

	if (!topic) {
		return PUBSUB_INVALID_PARAM;
	}
	if (!(p = find_topic(topic))) {
		return PUBSUB_TOPIC_NOT_EXIST;
	}

	return count_subscribers(p);
}

const char *pubsub_stringify_error(pubsub_error_t err)
{
	switch (err) {
	case PUBSUB_SUCCESS:
		return "success";
	case PUBSUB_ERROR:
		return "error";
	case PUBSUB_TOPIC_EXIST:
		return "topic already exist";
	case PUBSUB_TOPIC_NOT_EXIST:
		return "topic not exist";
	case PUBSUB_NO_MEMORY:
		return "no memory";
	case PUBSUB_INVALID_PARAM:
		return "invalid parameters";
	case PUBSUB_SUBSCRIBERS_EXIST:
		return "subscriber already exist";
	default:
		break;
	}

	return "unknown error";
}
