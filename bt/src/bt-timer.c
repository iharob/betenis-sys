#include <bt-timer.h>
#include <stdio.h>

static void
bt_timer_notify(union sigval data)
{
    if (data.sival_ptr == NULL)
        return;
    // Execute the notifier function
    ((bt_timer_notifier) data.sival_ptr)();
}

static void
bt_timer_get_remaining_time_until(struct tm *when, struct timespec *result)
{
    long int seconds;
    // Calculate the remainint time until the first trigger time
    seconds = mktime(when) - time(NULL);
    if (seconds < 0)
        seconds += 86400;
    // Update the timespec structure
    result->tv_sec = seconds;
    result->tv_nsec = 0;
}

timer_t
bt_setup_timer(const char *const when, bt_timer_notifier notifier)
{
    struct sigevent event = {0};
    timer_t timer;
    struct itimerspec timerspec = {0};
    struct tm start;
    time_t now;
    // Initialize the structure, save the notifier function
    // to call it when the timer triggers
    event.sigev_value.sival_ptr = notifier;
    event.sigev_notify = SIGEV_THREAD;
    event.sigev_notify_function = bt_timer_notify;
    // Set the interval to 1 day
    timerspec.it_interval.tv_sec = 86400;
    timerspec.it_interval.tv_nsec = 0;
    // Grab current time in seconds to calculate the start time
    time(&now);
    // Get the local time `now`
    localtime_r(&now, &start);
    // Scan the time string to extract the values
    if (sscanf(when, "%d:%d:%d", &start.tm_hour, &start.tm_min, &start.tm_sec) != 3)
        return (timer_t) -1;
    // Calculate the start time
    bt_timer_get_remaining_time_until(&start, &timerspec.it_value);
    // Create the timer
    timer_create(CLOCK_MONOTONIC, &event, &timer);
    // Setup the timer
    timer_settime(timer, 0, &timerspec, NULL);
    // Return it so it can be released
    return timer;
}
