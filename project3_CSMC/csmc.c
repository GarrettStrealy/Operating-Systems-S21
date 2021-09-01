#include "stdlib.h"
#include "unistd.h"
#include "stdio.h"
#include "pthread.h"
#include "time.h"
#include "semaphore.h"
#include "stdbool.h"

int nanosleep(const struct timespec *req, struct timespec *rem);

// user arguments
int STUDENTS, TUTORS, CHAIRS, HELP;

int empty_chairs = 0;
int total_sessions = 0;
int tutoring_now = 0;
int total_requests = 0;
int student_counter = 1;
int tutor_counter = 1;

pthread_mutex_t queue_lock;
pthread_mutex_t dequeue_lock;
pthread_mutex_t tutoring_now_lock;
pthread_mutex_t total_sessions_lock;
pthread_mutex_t stud_id_lock;
pthread_mutex_t tut_id_lock;
pthread_mutex_t empty_chairs_lock;
pthread_mutex_t student_lock;

sem_t stud_sem;
sem_t queue_sem;
sem_t coord_sem;
sem_t *session_sem;

time_t t;

struct student
{
    int stud_id;
    int tut_id;
    int priority;
    struct student *next;
};

struct waiting_student
{
    struct student *student;
    struct waiting_student *next;
};

struct student *all_studs_head;
struct waiting_student *queue_head;
int stud_to_queue;

struct waiting_student *dequeue()
{
    struct waiting_student *traversingNode = queue_head;
    struct waiting_student *dequeuedNode;

    // if there is one student in the queue
    if (!queue_head->next)
    {
        queue_head = NULL;

        return traversingNode;
    }
    // if there are two students
    else if (!queue_head->next->next)
    {
        dequeuedNode = queue_head->next;
        queue_head->next = NULL;

        return dequeuedNode;
    }

    // if there are more than two students
    while (traversingNode->next->next)
    {
        traversingNode = traversingNode->next;
    }

    dequeuedNode = traversingNode->next;
    traversingNode->next = NULL;

    return dequeuedNode;
}

void enqueue(struct waiting_student *stud_to_queue)
{
    pthread_mutex_lock(&dequeue_lock);
    struct waiting_student *traversingNode = queue_head;
    struct waiting_student *previousNode = NULL;
    bool notEmpty = queue_head != NULL;

    // if the queue is not empty
    if (notEmpty)
    {
        // find the node to insert before
        while (traversingNode)
        {
            if (traversingNode->student->priority < stud_to_queue->student->priority)
            {
                previousNode = traversingNode;
                traversingNode = traversingNode->next;
            }
            else
            {
                break;
            }
        }
        // if previous node exists
        if (previousNode)
        {
            // insert between traversing node and previous node
            stud_to_queue->next = traversingNode;
            previousNode->next = stud_to_queue;
        }
        // if no previous node
        else
        {
            // insert before head
            stud_to_queue->next = queue_head;
            queue_head = stud_to_queue;
        }
    }
    // if the queue is empty
    else
    {
        queue_head = stud_to_queue;
        queue_head->next = NULL;
    }
    pthread_mutex_unlock(&dequeue_lock);
}

void *student_routine(void *arg)
{
    struct student *studentNode = (struct student *)arg;
    int studentId;
    pthread_mutex_lock(&stud_id_lock);
    studentNode->stud_id = student_counter;
    student_counter++;
    pthread_mutex_unlock(&stud_id_lock);
    studentId = studentNode->stud_id;

    srand((unsigned)time(&t));

    while (studentNode->priority != 0)
    {
        pthread_mutex_lock(&empty_chairs_lock);
        if (empty_chairs == 0)
        {
            pthread_mutex_unlock(&empty_chairs_lock);
            printf("St: Student %d found no empty chair. Will try again later.\n", studentId);
            nanosleep((const struct timespec[]){{0, (rand() % 2000000L)}}, NULL);
            continue;
        }
        else
        {
            //  take chair
            empty_chairs--;
            printf("St: Student %d takes a seat. Empty chairs = %d.\n",
                   studentId, empty_chairs);
            pthread_mutex_unlock(&empty_chairs_lock);

            pthread_mutex_lock(&student_lock);

            // set student as the next to be queued
            pthread_mutex_lock(&queue_lock);
            stud_to_queue = studentId;
            pthread_mutex_unlock(&queue_lock);

            // signal arrival to coordinator
            sem_post(&stud_sem);

            // wait for coordinator to signal queue placement
            sem_wait(&queue_sem);

            pthread_mutex_unlock(&student_lock);

            // wait for tutor
            sem_wait(&session_sem[studentId]);

            // simulate being tutored for 2 ms
            nanosleep((const struct timespec[]){{0, 200000L}}, NULL);
            printf("St: Student %d received help from Tutor %d.\n",
                   studentId, studentNode->tut_id);

            // decrease priority
            studentNode->priority--;
        }
    }
}

void *tutor_routine()
{
    struct student *studentToTutor;
    int tutorId;
    pthread_mutex_lock(&tut_id_lock);
    tutorId = tutor_counter;
    tutor_counter++;
    pthread_mutex_unlock(&tut_id_lock);

    while (1)
    {
        // wait for coordinator
        sem_wait(&coord_sem);

        // get the next student
        pthread_mutex_lock(&dequeue_lock);
        studentToTutor = dequeue()->student;
        pthread_mutex_unlock(&dequeue_lock);

        // set the tutor for the student
        studentToTutor->tut_id = tutorId;

        pthread_mutex_lock(&tutoring_now_lock);
        tutoring_now++;
        pthread_mutex_unlock(&tutoring_now_lock);

        // signal the student
        sem_post(&session_sem[studentToTutor->stud_id]);

        // simulate tutoring for 2 ms
        nanosleep((const struct timespec[]){{0, 200000L}}, NULL);

        pthread_mutex_lock(&tutoring_now_lock);
        pthread_mutex_lock(&total_sessions_lock);
        total_sessions++;
        printf("Tu: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n",
               studentToTutor->stud_id, studentToTutor->tut_id, tutoring_now, total_sessions);
        tutoring_now--;
        pthread_mutex_unlock(&total_sessions_lock);
        pthread_mutex_unlock(&tutoring_now_lock);
    }
}

void *coordinator_routine()
{
    int nextStudentId;
    struct student *nextStudent;
    struct waiting_student *nextWaiting;
    struct student *traversingNode;

    while (1)
    {
        // wait for student to signal arrival
        sem_wait(&stud_sem);

        // increment total help requests received
        total_requests++;

        // get next student
        pthread_mutex_lock(&queue_lock);
        nextStudentId = stud_to_queue;
        pthread_mutex_unlock(&queue_lock);

        // signal to student they have been queued
        sem_post(&queue_sem);

        traversingNode = all_studs_head;

        // find next student
        while (traversingNode != NULL)
        {
            if (traversingNode->stud_id == nextStudentId)
            {
                break;
            }
            traversingNode = traversingNode->next;
        }
        nextStudent = traversingNode;

        nextWaiting = malloc(sizeof(struct waiting_student *));
        nextWaiting->student = nextStudent;

        enqueue(nextWaiting);

        pthread_mutex_lock(&empty_chairs_lock);
        printf("Co: Student %d with priority %d in the queue. Waiting students now = %d. Total requests = %d.\n",
               nextStudentId, nextStudent->priority, CHAIRS - empty_chairs - 1, total_requests);
        empty_chairs++;
        pthread_mutex_unlock(&empty_chairs_lock);

        // signal tutor
        sem_post(&coord_sem);
    }
}

int main(int argc, char *argv[])
{
    long i;

    STUDENTS = atoi(argv[1]);
    TUTORS = atoi(argv[2]);
    CHAIRS = atoi(argv[3]);
    HELP = atoi(argv[4]);

    empty_chairs = CHAIRS;

    sem_init(&stud_sem, 0, 0);
    sem_init(&queue_sem, 0, 0);
    sem_init(&coord_sem, 0, 0);

    pthread_t *student_threads;
    pthread_t *tutor_threads;
    pthread_t coordinator_thread;

    student_threads = malloc(STUDENTS * sizeof(pthread_t));
    tutor_threads = malloc(TUTORS * sizeof(pthread_t));

    session_sem = (sem_t *)malloc(STUDENTS * sizeof(sem_t));
    struct student *student_to_add;

    for (i = 0; i < STUDENTS; i++)
    {
        sem_init(&session_sem[i + 1], 0, 0);

        // add student to list of students
        student_to_add = malloc(sizeof(struct student));
        student_to_add->priority = HELP;

        pthread_create(&student_threads[i], NULL, student_routine, (void *)student_to_add);

        if (all_studs_head != NULL)
        {
            student_to_add->next = all_studs_head;
        }
        all_studs_head = student_to_add;
    }

    for (i = 0; i < TUTORS; i++)
    {
        pthread_create(&tutor_threads[i], NULL, tutor_routine, (void *)i);
    }

    pthread_create(&coordinator_thread, NULL, coordinator_routine, NULL);

    for (i = 0; i < STUDENTS; i++)
    {
        pthread_join(student_threads[i], NULL);
    }

    for (i = 0; i < TUTORS; i++)
    {
        pthread_cancel(tutor_threads[i]);
    }

    pthread_cancel(coordinator_thread);
}