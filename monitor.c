#include <wiringPi.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>
#include <signal.h>
#include <libpq-fe.h>
#include <string.h>

bool stopProgram = false;

void sig_handler(int signo)
{
	if (signo == SIGINT || signo == SIGUSR1) {
  		printf("\n");
		stopProgram = true;
	}
}

void pg_exit(PGconn *conn, PGresult *res) {
	PQclear(res);
	PQfinish(conn);
}

void do_exit(PGconn *conn, PGresult *res) {
	pg_exit(conn, res);
	fprintf(stderr, "PG_ERROR: %s\n", PQerrorMessage(conn));
	exit(1);
}

bool pg_bad_result(PGresult *res) {
	return (PQresultStatus(res) != PGRES_COMMAND_OK) ? true : false;
}

bool pg_bad_data(PGresult *res) {
	return (PQresultStatus(res) != PGRES_TUPLES_OK) ? true : false;
}

static double TimeSpecToSeconds(struct timespec* ts)
{
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

static double getElapsedTime(struct timespec* start, struct timespec* end, bool roundToMinute)
{
	double exact_total = TimeSpecToSeconds(end) - TimeSpecToSeconds(start);
	if(!roundToMinute) {
		return exact_total;
	} else {
		int total = round(exact_total);
		int roundTo = 60;
		int leftover = total % roundTo;
		if(leftover > 30) {
			return total + roundTo - (total % roundTo);
		} else {
			return total - leftover;
		}
	}
}

void databaseSetup(PGconn *conn) {
	// create the bay status table
	PGresult *res = PQexec(conn, "CREATE TABLE IF NOT EXISTS bay_status (bay INT NOT NULL, timer_running BOOLEAN NOT NULL DEFAULT FALSE, pump_running BOOLEAN NOT NULL DEFAULT FALSE, timer_runtime NUMERIC(12, 2) NOT NULL DEFAULT 0, pump_runtime NUMERIC(12, 2) NOT NULL DEFAULT 0, PRIMARY KEY (bay));");
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);

	// create the bay status entries
	res = PQexec(conn, "SELECT bay FROM bay_status WHERE bay = 1");
	if(pg_bad_data(res)) do_exit(conn, res);
	if(PQntuples(res) < 1) {
		PQclear(res);
		res = PQexec(conn, "INSERT INTO bay_status (bay, timer_running, pump_running) VALUES (1, false, false);");
		if(pg_bad_result(res)) {
			printf("\nCould not create status for Bay 1\n");
			do_exit(conn, res);
		}
	}
	PQclear(res);
	
	res = PQexec(conn, "SELECT bay FROM bay_status WHERE bay = 2");
	if(pg_bad_data(res)) do_exit(conn, res);
	if(PQntuples(res) < 1) {
		PQclear(res);
		res = PQexec(conn, "INSERT INTO bay_status (bay, timer_running, pump_running) VALUES (2, false, false);");
		if(pg_bad_result(res)) {
			printf("\nCould not create status for Bay 2\n");
			do_exit(conn, res);
		}
	}
	PQclear(res);
	
	res = PQexec(conn, "SELECT bay FROM bay_status WHERE bay = 3");
	if(pg_bad_data(res)) do_exit(conn, res);
	if(PQntuples(res) < 1) {
		PQclear(res);
		res = PQexec(conn, "INSERT INTO bay_status (bay, timer_running, pump_running) VALUES (3, false, false);");
		if(pg_bad_result(res)) {
			printf("\nCould not create status for Bay 3\n");
			do_exit(conn, res);
		}
	}
	PQclear(res);
	
	res = PQexec(conn, "SELECT bay FROM bay_status WHERE bay = 4");
	if(pg_bad_data(res)) do_exit(conn, res);
	if(PQntuples(res) < 1) {
		PQclear(res);
		res = PQexec(conn, "INSERT INTO bay_status (bay, timer_running, pump_running) VALUES (4, false, false);");
		if(pg_bad_result(res)) {
			printf("\nCould not create status for Bay 4\n");
			do_exit(conn, res);
		}
	}
	PQclear(res);

	// set all statuses to false
	res = PQexec(conn, "UPDATE bay_status SET timer_running = false, pump_running = false;");
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);


	res = PQexec(conn, "CREATE TABLE IF NOT EXISTS bay_sessions (id BIGSERIAL, bay INT NOT NULL, timer_time NUMERIC(14,2), pump_time NUMERIC(14,2), timestamp TIMESTAMP DEFAULT current_timestamp, PRIMARY KEY (id));");
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);


	res = PQexec(conn, "CREATE TABLE IF NOT EXISTS bay_maintenance_inserts (id BIGSERIAL, bay INT NOT NULL, timestamp TIMESTAMP DEFAULT current_timestamp, PRIMARY KEY (id));");
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);
}

int main (void)
{
	if (signal(SIGINT, sig_handler) == SIG_ERR)
		printf("\ncan't catch SIGINT\n");
	if (signal(SIGUSR1, sig_handler) == SIG_ERR)
		printf("\ncan't catch SIGUSR1\n");

	// CONNECT TO POSTGRESQL
	PGconn *conn = PQconnectdb("user=washman password=cotton dbname=carwash");

	if(PQstatus(conn) == CONNECTION_BAD) {
		fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		exit(1);
	}

	// MAKE SURE WE HAVE OUR TABLES
	databaseSetup(conn);

	// SET UP PREPARED STATEMENT FOR BAY STATUS
	char *statement = "UPDATE bay_status SET timer_running = $1, pump_running = $2 WHERE bay = $3;";
	PGresult *res = PQprepare(conn, "UPDATE_BAY_STATUS", statement, 3, NULL);
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);


	// SET UP PREPARED STATEMENT FOR BAY STATUS RUNTIME
	char *statement2 = "UPDATE bay_status SET timer_runtime = $1, pump_runtime = $2 WHERE bay = $3;";
	res = PQprepare(conn, "UPDATE_BAY_STATUS_RUNTIME", statement2, 3, NULL);
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);


	// SET UP PREPARED STATEMENT FOR BAY MAINTENANCE INSERTS
	char *statement3 = "INSERT INTO bay_sessions (bay, timer_time, pump_time) VALUES ($1, $2, $3);";
	res = PQprepare(conn, "BAY_SESSION_INSERT", statement3, 3, NULL);
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);


	// SET UP PREPARED STATEMENT FOR BAY MAINTENANCE INSERTS
	char *statement4 = "INSERT INTO bay_maintenance_inserts (bay) VALUES ($1);";
	res = PQprepare(conn, "MAINTENANCE_INSERT", statement4, 1, NULL);
	if(pg_bad_result(res)) do_exit(conn, res);
	PQclear(res);



	// INITIAL SETUP

	printf("INITIALIZING...\n");
	/* 
	    LAYOUT: bayPins[bay][GPIO pin]
	    PIN DEFINTIONS: {
			0: TIMER_ON_RELAY_INPUT,	// records total paid time
			1: PUMP_ON_RELAY_INPUT,		// records total pump time
			2: INSERT_COIN_RELAY_OUTPUT,// NOT USED! remotely "inserts" a coin
			3: MAINTENANCE_INSERT		// records when a coin is "inserted" from pump room
		}
		pins listed are GPIO numbers not physical numbers (physical commented to right)
	*/

	int REBOOT_PIN = 25; // 37
	int WIPE_PIN = 14; // 23

	int bayPins[4][4] = {
		{7, 0, 2, 3},     // 7, 11, 13, 15
		{1, 4, 5, 6},     // 12, 16, 18, 22
		{21, 22, 23, 24}, // 29, 31, 33, 35
		{26, 27, 28, 29}, // 32, 36, 38, 40
	};

	/*
		LAYOUT: bayTimers[bay][timer]
		DEFINITION: {
			0: TIMER_START,
			1: TIMER_END,
			2: PUMP_START,
			3: PUMP_END
		}
	*/
	struct timespec bayTimers[4][4];

	/*
		LAYOUT: bayTimerThresholds[bay][timer/pump start/stop]
		DEFINITION: {
			0: TIMER_START_COUNT,
			1: TIMER_STOP_COUNT,
			2: PUMP_START_COUNT,
			3: PUMP_STOP_COUNT
		}
	*/
	int bayTimerThresholds[4][4] = {
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
	};

	/*
		LAYOUT: elapsedTime[bay][timer or pump]
		DEFINITION: {
			0: TIMER_ELAPSED_TIME,
			1: PUMP_ELAPSED_TIME
		}
	*/
	double elapsedTime[4][2] = {
		{0,0},
		{0,0},
		{0,0},
		{0,0},
	};

	double pumpSessionElapsedTime[4] = {0,0,0,0};

	/*
		LAYOUT: bayRunning[bay][timer or pump]
		DEFINITION: {
			0: TIMER_RUNNING,
			1: PUMP_RUNNING
		}
	*/
	bool bayRunning[4][2] = {
		{false, false},
		{false, false},
		{false, false},
		{false, false},
	};

	bool bayInsertState[4] = {true, true, true, true};

	int bayInsertCounter[4] = {0,0,0,0};

	// how many cycles to wait before confirming a start or stop of relay
	int threshold = 20;
	// how many cycles to wait before confirming a coin insert
	int MAINTENANCE_THRESHOLD = 5;

	// SETUP GPIO PINS
	wiringPiSetup();
	// activate shutdown pin
	pinMode(REBOOT_PIN, INPUT);
	pullUpDnControl(REBOOT_PIN, PUD_UP);
	pinMode(WIPE_PIN, INPUT);
	pullUpDnControl(WIPE_PIN, PUD_UP);
	int i = 0;
	for(i = 0; i < 4; i++) {
		pinMode(bayPins[i][0], INPUT);
		pinMode(bayPins[i][1], INPUT);
		pinMode(bayPins[i][2], INPUT);
		pinMode(bayPins[i][3], INPUT);
		pullUpDnControl(bayPins[i][0], PUD_UP);
		pullUpDnControl(bayPins[i][1], PUD_UP);
		pullUpDnControl(bayPins[i][2], PUD_UP);
		pullUpDnControl(bayPins[i][3], PUD_UP);
	};

	printf("RUNNING\n");

	const char *strue = "true";
	const char *sfalse = "false";

	char bay_string[1];
	char bay_timer_runtime[16];
	char bay_pump_runtime[16];
	char bay_session_timer_time[16];
	char bay_session_pump_time[16];

	int counter = 0;
	int COUNTER_MAX = 10;

	int reboot_pin_counter = 0;
	int reboot_threshold = 100;
	int shutdown_threshold = 500;
	int wipe_pin_counter = 0;
	int wipe_threshold = 1000;

	// main loop
	while (stopProgram == false) {
		counter ++;
		if(counter > COUNTER_MAX) {
			counter = 0;
		}
		
		// loop over each bay
		for(i = 0; i < 4; i++) {

			// convert the bay number to const char for PG use
			sprintf(bay_string, "%d", i + 1);

			if(bayRunning[i][0] == true && counter > COUNTER_MAX - 1) {
				clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][1]);
				clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][3]);
				elapsedTime[i][0] = getElapsedTime(&bayTimers[i][0], &bayTimers[i][1], false);
				elapsedTime[i][1] = getElapsedTime(&bayTimers[i][2], &bayTimers[i][3], false);

				double elapsedPumpTime = (bayRunning[i][1] == true)
					? pumpSessionElapsedTime[i] + elapsedTime[i][1]
					: pumpSessionElapsedTime[i];

				sprintf(bay_timer_runtime, "%lf", elapsedTime[i][0]);
				sprintf(bay_pump_runtime, "%lf", elapsedPumpTime);

				const char *updateRuntimeParamValues[3] = {bay_timer_runtime, bay_pump_runtime, bay_string};
		        PGresult *res = PQexecPrepared(conn, "UPDATE_BAY_STATUS_RUNTIME", 3, updateRuntimeParamValues, NULL, NULL, 0);
		        if(pg_bad_result(res)) do_exit(conn, res);
		        PQclear(res);
			}

			// HANDLE TIMER RELAY (INDEX 0)
			if(digitalRead(bayPins[i][0]) == LOW && bayRunning[i][0] == false) {
				if(bayTimerThresholds[i][0] < threshold) {
					bayTimerThresholds[i][0]++;
				} else {
					bayTimerThresholds[i][0] = 0;
					bayRunning[i][0] = true;
					clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][0]);

					// update bay status
					const char *updateParamValues[3] = {bayRunning[i][0] == true ? strue : sfalse, bayRunning[i][1] == true ? strue : sfalse, bay_string};
			        PGresult *res = PQexecPrepared(conn, "UPDATE_BAY_STATUS", 3, updateParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);
				}
			} else if(digitalRead(bayPins[i][0]) == HIGH && bayRunning[i][0] == true) {

				if(bayTimerThresholds[i][1] < threshold) {
					bayTimerThresholds[i][1]++;
				} else {
					bayTimerThresholds[i][1] = 0;
					clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][1]);
					clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][3]);
					elapsedTime[i][0] = getElapsedTime(&bayTimers[i][0], &bayTimers[i][1], true);
					elapsedTime[i][1] = getElapsedTime(&bayTimers[i][2], &bayTimers[i][3], false) + pumpSessionElapsedTime[i];
					printf("BAY %d TIMER ELAPSED: %f seconds\n", (int)i + 1, elapsedTime[i][0]);
					bayRunning[i][0] = false;

					// record session
					sprintf(bay_session_timer_time, "%lf", elapsedTime[i][0]);
					sprintf(bay_session_pump_time, "%lf", elapsedTime[i][1]);
					const char *insertParamValues[3] = {bay_string, bay_session_timer_time, bay_session_pump_time};
			        PGresult *res = PQexecPrepared(conn, "BAY_SESSION_INSERT", 3, insertParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);

					// update bay status
					const char *updateParamValues[3] = {bayRunning[i][0] == true ? strue : sfalse, bayRunning[i][1] == true ? strue : sfalse, bay_string};
			        res = PQexecPrepared(conn, "UPDATE_BAY_STATUS", 3, updateParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);

		        	// update bay status timer_runtime
					const char *updateRuntimeParamValues[3] = {"0", "0", bay_string};
			        res = PQexecPrepared(conn, "UPDATE_BAY_STATUS_RUNTIME", 3, updateRuntimeParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);
				}
			}

			// HANDLE PUMP RELAY (INDEX 1)
			if(digitalRead(bayPins[i][1]) == LOW && bayRunning[i][1] == false) {
				if(bayTimerThresholds[i][2] < threshold) {
					bayTimerThresholds[i][2]++;
				} else {
					bayTimerThresholds[i][2] = 0;
					bayRunning[i][1] = true;
					clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][2]);


					// update bay status
					const char *updateParamValues[3] = {bayRunning[i][0] == true ? strue : sfalse, bayRunning[i][1] == true ? strue : sfalse, bay_string};
			        PGresult *res = PQexecPrepared(conn, "UPDATE_BAY_STATUS", 3, updateParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);
				}
			} else if(digitalRead(bayPins[i][1]) == HIGH && bayRunning[i][1] == true) {

				if(bayTimerThresholds[i][3] < threshold) {
					bayTimerThresholds[i][3]++;
				} else {
					bayTimerThresholds[i][3] = 0;
					clock_gettime(CLOCK_MONOTONIC, &bayTimers[i][3]);
					elapsedTime[i][1] = getElapsedTime(&bayTimers[i][2], &bayTimers[i][3], false);
					printf("BAY %d PUMP ELAPSED: %f seconds\n", (int)i + 1, elapsedTime[i][1]);
					bayRunning[i][1] = false;

					if(bayRunning[i][0] == false) {
						pumpSessionElapsedTime[i] = 0;
						elapsedTime[i][1] = 0;
					} else {
						pumpSessionElapsedTime[i] = pumpSessionElapsedTime[i] + elapsedTime[i][1];
						bayTimers[i][2] = bayTimers[i][3];
					}


					// update bay status
					const char *updateParamValues[3] = {bayRunning[i][0] == true ? strue : sfalse, bayRunning[i][1] == true ? strue : sfalse, bay_string};
			        PGresult *res = PQexecPrepared(conn, "UPDATE_BAY_STATUS", 3, updateParamValues, NULL, NULL, 0);
			        if(pg_bad_result(res)) do_exit(conn, res);
			        PQclear(res);
				}
			}

			// HANDLE REMOTE INSERT COIN RELAY (INDEX 2)
			// hehe nothing here

			// HANDLE MAINTENANCE COIN INSERT (INDEX 3)
			if(digitalRead(bayPins[i][3]) == LOW) {
				bayInsertCounter[i] = 0;
				bayInsertState[i] = false;
			} else if(digitalRead(bayPins[i][3]) == HIGH) {
				if(bayInsertState[i] == false) {
					if(bayInsertCounter[i] < MAINTENANCE_THRESHOLD) {
						bayInsertCounter[i]++;
					} else {
						bayInsertCounter[i] = 0;
						bayInsertState[i] = true;
						printf("Bay %d insert\n", i + 1);
						// update bay status
				        PGresult *res = PQexecPrepared(conn, "MAINTENANCE_INSERT", 1, (const char*[1]){bay_string}, NULL, NULL, 0);
				        if(pg_bad_result(res)) do_exit(conn, res);
				        PQclear(res);
					}
				}
			}

		} // end for loop

		// handle reboot pin
		if(digitalRead(REBOOT_PIN) == LOW) {
			reboot_pin_counter++;
		} else if(digitalRead(REBOOT_PIN) == HIGH) {
			if(reboot_pin_counter > reboot_threshold && reboot_pin_counter < shutdown_threshold) {
				system("shutdown -r now");
			} else if(reboot_pin_counter > shutdown_threshold) {
				system("shutdown now");
			}
			if(reboot_pin_counter > 0) {
				reboot_pin_counter --;
			}
		}

		// handle wipe pin
		if(digitalRead(WIPE_PIN) == LOW) {
			wipe_pin_counter++;
		} else if(digitalRead(WIPE_PIN) == HIGH) {
			if(wipe_pin_counter > wipe_threshold) {
				system("PGPASSWORD=cotton psql -U washman -d carwash -c 'DELETE FROM bay_sessions; DELETE FROM bay_maintenance_inserts;'");
				system("shutdown -r now");
			}
			if(wipe_pin_counter > 0) {
				wipe_pin_counter --;
			}
		}

		nanosleep((const struct timespec[]){{0, 10000000L}}, NULL);
	} // end while

	PQfinish(conn);

	// CLEANUP: pull down pins on exit
	for(i = 0; i < 4; i++) {
		pullUpDnControl(bayPins[i][0], PUD_DOWN);
		pullUpDnControl(bayPins[i][1], PUD_DOWN);
		pullUpDnControl(bayPins[i][2], PUD_DOWN);
		pullUpDnControl(bayPins[i][3], PUD_DOWN);
	}

	pullUpDnControl(REBOOT_PIN, PUD_DOWN);
	pullUpDnControl(WIPE_PIN, PUD_DOWN);

	printf("FINISHED \n");
	return 0;
}
