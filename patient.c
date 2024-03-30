#include <stdio.h> // Include standard input/output library for printing to console and reading input
#include <stdlib.h> // Include standard library for memory allocation, process control, conversions, etc.
#include <string.h> // Include string handling library
#include <unistd.h> // Include POSIX operating system API for access to POSIX system call API like fork(), sleep()
#include <sys/ipc.h> // Include for IPC (Inter-Process Communication) to use keys
#include <sys/sem.h> // Include for semaphore operations
#include <sys/shm.h> // Include for shared memory operations
#include <sys/types.h> // Include for system data type definitions
#include <sys/wait.h> // Include for waiting for processes to change state

#define SHM_KEY 0x1234 // Define a unique key for shared memory
#define SEM_MUTEX_KEY 0x5678 // Define a unique key for the mutex semaphore
#define SEM_PATIENT_KEY 0x91011 // Define a unique key for the patient semaphore
#define MAX_PATIENTS 10 // Maximum number of patients that can be handled

// Structure to hold patient information
typedef struct {
    int id; // Patient ID
    char name[100]; // Patient name
} Patient;

// Structure for shared memory to hold pharmacy data
typedef struct {
    Patient patients[MAX_PATIENTS]; // Array of patients
    int count; // Number of patients currently in the pharmacy
} PharmacySharedMemory;

// Lock the semaphore (decrement)
void sem_lock(int sem_id) {
    struct sembuf sb = {0, -1, 0}; // sembuf structure for semaphore operations
    semop(sem_id, &sb, 1); // Perform semaphore operation to lock/decrement
}

// Unlock the semaphore (increment)
void sem_unlock(int sem_id) {
    struct sembuf sb = {0, 1, 0}; // sembuf structure for semaphore operations
    semop(sem_id, &sb, 1); // Perform semaphore operation to unlock/increment
}

// Cleanup shared resources like shared memory and semaphores
void cleanup(int shm_id, int sem_mutex, int sem_patient) {
    shmctl(shm_id, IPC_RMID, NULL); // Remove shared memory
    semctl(sem_mutex, 0, IPC_RMID, NULL); // Remove mutex semaphore
    semctl(sem_patient, 0, IPC_RMID, NULL); // Remove patient semaphore
}

int main() {
    printf("Starting the pharmacy program.\n");

    // Initialize shared memory
    int shm_id = shmget(SHM_KEY, sizeof(PharmacySharedMemory), 0644 | IPC_CREAT);
    if (shm_id < 0) {
        perror("shmget");
        exit(1);
    }

    // Attach shared memory
    PharmacySharedMemory *shm_ptr = (PharmacySharedMemory *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("shmat");
        cleanup(shm_id, -1, -1); 
        exit(1);
    }
    shm_ptr->count = 0; // Initialize the count of patients to 0

    // Initialize semaphores
    int sem_mutex = semget(SEM_MUTEX_KEY, 1, 0644 | IPC_CREAT);
    int sem_patient = semget(SEM_PATIENT_KEY, 1, 0644 | IPC_CREAT);

    if (sem_mutex < 0 || sem_patient < 0) {
        perror("semget");
        cleanup(shm_id, sem_mutex, sem_patient);
        exit(1);
    }

    // Set initial values for semaphores
    semctl(sem_mutex, 0, SETVAL, 1); // Mutex semaphore initialized to 1 (unlocked)
    semctl(sem_patient, 0, SETVAL, MAX_PATIENTS); // Patient semaphore initialized to MAX_PATIENTS

    pid_t pid = fork(); // Fork a new process

    if (pid < 0) { // Check if fork failed
        perror("fork");
        cleanup(shm_id, sem_mutex, sem_patient);
        exit(1);
    }

    if (pid == 0) { // Child process: Process 2 - Jab Administration
        printf("Child process started.\n");
        for (int i = 0; i < 5; ++i) { // Simulate processing 5 patients
            sem_lock(sem_mutex); // Lock the semaphore before accessing shared memory
            if (shm_ptr->count > 0) { // Check if there are any patients
                // Administer jab to the first patient in the queue
                printf("Administering jab to patient %d: %s\n", shm_ptr->patients[0].id, shm_ptr->patients[0].name);
                // Shift all patients up in the queue
                for (int j = 0; j < shm_ptr->count - 1; j++) {
                    shm_ptr->patients[j] = shm_ptr->patients[j + 1];
                }
                shm_ptr->count--; // Decrement the patient count
                sem_unlock(sem_mutex); // Unlock the semaphore after accessing shared memory
                sem_unlock(sem_patient); // Signal that a patient slot is now available
                sleep(2); // Simulate time to administer jab
            } else {
                sem_unlock(sem_mutex); // Unlock the semaphore if no patients
                printf("No patients. Child is waiting.\n");
                sleep(2); // Wait for patients to arrive
            }
        }
        exit(0); // Ensure child process exits properly
    } else {
        // Parent process: Process 1 - Adding Patients
        printf("Parent process adding patients.\n");
        for (int i = 0; i < 5; ++i) { // Simulate adding 5 patients
            sem_lock(sem_mutex); // Lock the semaphore before accessing shared memory
            int count = shm_ptr->count;
            if (count < MAX_PATIENTS) { // Check if there's space for more patients
                // Add a new patient to the queue
                shm_ptr->patients[count].id = i;
                snprintf(shm_ptr->patients[count].name, 100, "Patient %d", i);
                shm_ptr->count++;
                printf("Added %s to the queue.\n", shm_ptr->patients[count].name);
                sem_unlock(sem_mutex); // Unlock the semaphore after adding a patient
                sem_unlock(sem_patient); // Signal that a patient has been added
                sleep(1); // Simulate time between patient arrivals
            } else {
                sem_unlock(sem_mutex); // Unlock the semaphore if pharmacy is full
                printf("Pharmacy is full. Parent is waiting.\n");
                sleep(2); // Wait for space to add more patients
            }
        }
        wait(NULL); // Wait for the child process to finish
        cleanup(shm_id, sem_mutex, sem_patient); // Cleanup resources after processes finish
        printf("Parent process finished. Cleaned up resources.\n");
    }

    return 0;
}
