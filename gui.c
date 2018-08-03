#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <ncurses.h>
#include <libpq-fe.h>
#include <stdio.h>

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

int main ()
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

	// INIT NCURSES
	initscr();
	cbreak();
	noecho();
	curs_set(0);

	// get coordinates
	int xmax,ymax;
	getmaxyx(stdscr, ymax, xmax);

	int quads[4][2] = {
		{ymax/4, xmax/4},	// TOP LEFT
		{ymax/4, xmax/4 + xmax/2}, // TOP RIGHT
		{ymax/4 + ymax/2, xmax/4}, // BOTTOM LEFT
		{ymax/4 + ymax/2, xmax/4 + xmax/2} // BOTTOM RIGHT
	};

	int vertical_quad_width = ((xmax+4) / 4);
	int vertical_quad_height = ymax - 6;
	int vertical_quad_top = 4;

	int title_offset = -(ymax/4)/2;

	/* MAIN DATA STORAGE */
	bool bayRunning[4][2] = {
		{false, false},
		{false, false},
		{false, false},
		{false, false},
	};

	// timer - pump
	double bayCurrentRuntime[4][2] = {
		{0,0},
		{0,0},
		{0,0},
		{0,0},
	};

	// timer - pump
	double bayTotalRuntime[4][2] = {
		{0,0},
		{0,0},
		{0,0},
		{0,0},
	};

	double bayMaintenanceInserts[4] = {0,0,0,0};

	// gross, net, maintenance
	double bayMoneyTotals[4][3] = {
		{0,0,0},
		{0,0,0},
		{0,0,0},
		{0,0,0},
	};



	// setup color pairs
	start_color();
	init_pair(1, COLOR_BLACK, COLOR_GREEN);
	init_pair(2, COLOR_WHITE, COLOR_RED);
	init_pair(3, COLOR_BLACK, COLOR_YELLOW);
	init_pair(4, COLOR_GREEN, COLOR_BLACK);
	init_pair(5, COLOR_CYAN, COLOR_BLACK);

	time_t current_time;
    char* c_time_string;

	FILE *temperatureFile;
	double T;
    char* temp_string[20] = {0};

	char* bayTitle[16] = {0};
	char* pumpTitle[16] = {0};
	char* current_timer_time_string[16] = {0};
	char* current_pump_time_string[16] = {0};
	char* total_timer_time_string[16] = {0};
	char* total_pump_time_string[16] = {0};
	char* total_gross_money_string[16] = {0};
	char* total_net_money_string[16] = {0};
	char* total_maintenance_money_string[16] = {0};
	char* timerString[16] = {0};
	char* pumpString[16] = {0};

	int c = 0;
	int x = 0;
	while(stopProgram == false) {
		erase();

		// print time
    	current_time = time(NULL);
    	c_time_string = ctime(&current_time);
		mvprintw(0, xmax/2 - 2 - strlen((const char *)c_time_string) / 2, "%s", c_time_string);


		// print operating temperature
		temperatureFile = fopen ("/sys/class/thermal/thermal_zone0/temp", "r");
		if (temperatureFile == NULL) {
	        printf("Error opening file\n");
	        exit(1);
	    }

		fscanf (temperatureFile, "%lf", &T);
		T /= 1000;
		sprintf((unsigned char *)temp_string, " TEMP: %6.3f C. ", T);
		attron(COLOR_PAIR(3));
		mvprintw(0, 1, "%s", temp_string);
		attroff(COLOR_PAIR(3));
		fclose (temperatureFile);


		// collect bay statuses
		char *stm = "SELECT bay, timer_running, pump_running, timer_runtime, pump_runtime FROM bay_status ORDER BY bay ASC;";
		PGresult *res = PQexec(conn, stm);
		if(pg_bad_data(res)) do_exit(conn, res);
		for(x = 0; x < 4; x++) {
			int currentBay = atof(PQgetvalue(res, x, 0)) - 1;
			bayRunning[currentBay][0] = strcmp(PQgetvalue(res, x, 1), "f") != 0;
			bayRunning[currentBay][1] = strcmp(PQgetvalue(res, x, 2), "f") != 0;
			bayCurrentRuntime[currentBay][0] = atof(PQgetvalue(res, x, 3));
			bayCurrentRuntime[currentBay][1] = atof(PQgetvalue(res, x, 4));
		}
		PQclear(res);

		// collect bay totals
		char *stm2 = "SELECT bay, SUM(timer_time) as total_timer_time, SUM(pump_time) as total_pump_time FROM bay_sessions GROUP BY bay ORDER BY bay ASC;";
		res = PQexec(conn, stm2);
		if(pg_bad_data(res)) do_exit(conn, res);
		for(x = 0; x < PQntuples(res); x++) {
			int currentBay = atof(PQgetvalue(res, x, 0)) - 1;
			bayTotalRuntime[currentBay][0] = atof(PQgetvalue(res, x, 1));
			bayTotalRuntime[currentBay][1] = atof(PQgetvalue(res, x, 2));
		}
		PQclear(res);

		// collect bay maintenance inserts
		char *stm3 = "SELECT bay, count(*) as total FROM bay_maintenance_inserts GROUP BY bay ORDER BY bay ASC;";
		res = PQexec(conn, stm3);
		if(pg_bad_data(res)) do_exit(conn, res);
		for(x = 0; x < PQntuples(res); x++) {
			int currentBay = atof(PQgetvalue(res, x, 0)) - 1;
			bayMaintenanceInserts[currentBay] = atof(PQgetvalue(res, x, 1));
		}
		PQclear(res);

		// TOTAL UP MONEY
		double totalRevenue = 0;
		for(x = 0; x < 4; x++) {
			bayMoneyTotals[x][0] = (bayTotalRuntime[x][0] / 60) / 4;
			bayMoneyTotals[x][1] = ((bayTotalRuntime[x][0] / 60) / 4) - (bayMaintenanceInserts[x] * 0.25);
			bayMoneyTotals[x][2] = bayMaintenanceInserts[x] * 0.25;

			totalRevenue += bayMoneyTotals[x][1];
		}

		// print money total at bottom
		mvprintw(ymax - 1, (xmax / 2) - 15, "TOTAL REVENUE: $%.2f", totalRevenue);

		for(x = 0; x < 4; x++) {
			if(bayCurrentRuntime[x][0] >= 3599.99) {
				sprintf((unsigned char *)current_timer_time_string, "%d hrs %d mins %d secs", (int)bayCurrentRuntime[x][0]/3600, ((int)bayCurrentRuntime[x][0] % 3600) / 60, (int)bayCurrentRuntime[x][0] % 60);
			} else if (bayCurrentRuntime[x][0] > 59.99 && bayCurrentRuntime[x][0] < 3599.99) {
				sprintf((unsigned char *)current_timer_time_string, "%d mins %d secs", (int)bayCurrentRuntime[x][0] / 60, (int)bayCurrentRuntime[x][0] % 60);
			} else {
				sprintf((unsigned char *)current_timer_time_string, "%.2f seconds", bayCurrentRuntime[x][0]);
			}
			if(bayCurrentRuntime[x][1] >= 3599.99) {
				sprintf((unsigned char *)current_pump_time_string, "%d hrs %d mins %d secs", (int)bayCurrentRuntime[x][1]/3600, ((int)bayCurrentRuntime[x][1] % 3600) / 60, (int)bayCurrentRuntime[x][1] % 60);	
			} else if (bayCurrentRuntime[x][1] > 59.99 && bayCurrentRuntime[x][1] < 3599.99) {
				sprintf((unsigned char *)current_pump_time_string, "%d minutes %d secs", (int)bayCurrentRuntime[x][1] / 60, (int)bayCurrentRuntime[x][1] % 60);	
			} else {
				sprintf((unsigned char *)current_pump_time_string, "%.2f seconds", bayCurrentRuntime[x][1]);
			}
			if(bayTotalRuntime[x][0] >= 3599.99) {
				sprintf((unsigned char *)total_timer_time_string, "%d hrs %d mins", (int)bayTotalRuntime[x][0]/3600, ((int)bayTotalRuntime[x][0] % 3600) / 60);
			} else if (bayTotalRuntime[x][0] > 59.99 && bayTotalRuntime[x][0] < 3599.99) {
				sprintf((unsigned char *)total_timer_time_string, "%d minutes", (int)bayTotalRuntime[x][0] / 60);
			} else {
				sprintf((unsigned char *)total_timer_time_string, "%.2f seconds", bayTotalRuntime[x][0]);
			}
			if(bayTotalRuntime[x][1] >= 3599.99) {
				sprintf((unsigned char *)total_pump_time_string, "%d hrs %d mins %d secs", (int)bayTotalRuntime[x][1]/3600, ((int)bayTotalRuntime[x][1] % 3600) / 60, (int)bayTotalRuntime[x][1] % 60);	
			} else if (bayTotalRuntime[x][1] > 59.99 && bayTotalRuntime[x][1] < 3599.99) {
				sprintf((unsigned char *)total_pump_time_string, "%d mins %d secs", (int)bayTotalRuntime[x][1] / 60, (int)bayTotalRuntime[x][1] % 60);	
			} else {
				sprintf((unsigned char *)total_pump_time_string, "%.2f seconds", bayTotalRuntime[x][1]);
			}

			sprintf((unsigned char *)total_gross_money_string, "$%.2f", bayMoneyTotals[x][0]);
			sprintf((unsigned char *)total_net_money_string, "$%.2f", bayMoneyTotals[x][1]);
			sprintf((unsigned char *)total_maintenance_money_string, "-$%.2f", bayMoneyTotals[x][2]);
			sprintf((unsigned char *)bayTitle,  "       BAY %d        ", (x + 1));
			sprintf((unsigned char *)pumpTitle, "       PUMP %d       ", (x + 1));

			int quad_x_center = ((x * vertical_quad_width) + vertical_quad_width/2) + ((x > 2) ? -1 : 0);
			int quad_x_left = x * vertical_quad_width + ((x > 2) ? 0 : 1);

			// TIMER, CURRENT TIMER, TOTAL TIMER, TOTAL MONEY, TOTAL INSERTS, NET MONEY
			attron(COLOR_PAIR((bayRunning[x][0]) ? 1 : 2));
			if(bayRunning[x][0]) {
				mvprintw(vertical_quad_top, quad_x_center - 9, "              ");	
				mvprintw(vertical_quad_top+2, quad_x_center - 9, "              ");	
			}
			mvprintw(vertical_quad_top + 1, quad_x_center - strlen((const char *)bayTitle)/2 - 2, (const char *)bayTitle);
			attroff(COLOR_PAIR((bayRunning[x][0]) ? 1 : 2));


			if(bayRunning[x][0]) {
				attron(COLOR_PAIR(5));
				mvprintw(vertical_quad_top+3, quad_x_center - strlen((const char *)current_timer_time_string)/2 - 2, (const char *)current_timer_time_string);
				attroff(COLOR_PAIR(5));
			}


			mvprintw(vertical_quad_top+5, quad_x_left, "TOTAL TIMER RUNTIME:");
			attron(COLOR_PAIR(4));
			mvprintw(vertical_quad_top+6, quad_x_left + 1, (const char *)total_timer_time_string);
			attroff(COLOR_PAIR(4));

			mvprintw(vertical_quad_top+7, quad_x_left, "GROSS REVENUE:");
			attron(COLOR_PAIR(4));
			mvprintw(vertical_quad_top+8, quad_x_left + 1, (const char *)total_gross_money_string);
			attroff(COLOR_PAIR(4));

			mvprintw(vertical_quad_top+9, quad_x_left, "MANUAL COINS:");
			attron(COLOR_PAIR(4));
			mvprintw(vertical_quad_top+10, quad_x_left, (const char *)total_maintenance_money_string);
			attroff(COLOR_PAIR(4));

			mvprintw(vertical_quad_top+11, quad_x_left, "NET REVENUE:");
			attron(COLOR_PAIR(4));
			mvprintw(vertical_quad_top+12, quad_x_left + 1, (const char *)total_net_money_string);
			attroff(COLOR_PAIR(4));


			// PUMP, CURRENT PUMP TIMER, TOTAL PUMP TIMER, 
			attron(COLOR_PAIR((bayRunning[x][1]) ? 1 : 2));
			if(bayRunning[x][0]) {
				mvprintw(vertical_quad_top + 14, quad_x_center - 9, "              ");
				mvprintw(vertical_quad_top + 16, quad_x_center - 9, "              ");
			}
			mvprintw(vertical_quad_top + 15, quad_x_center - strlen((const char *)pumpTitle)/2 - 2, (const char *)pumpTitle);
			attroff(COLOR_PAIR((bayRunning[x][1]) ? 1 : 2));


			if(bayRunning[x][0]) {
				attron(COLOR_PAIR(5));
				mvprintw(vertical_quad_top+17, quad_x_center - strlen((const char *)current_pump_time_string)/2 - 2, (const char *)current_pump_time_string);
				attroff(COLOR_PAIR(5));
			}

			mvprintw(vertical_quad_top+19, quad_x_left, "TOTAL PUMP RUNTIME:");
			attron(COLOR_PAIR(4));
			mvprintw(vertical_quad_top+20, quad_x_left, (const char *)total_pump_time_string);
			attroff(COLOR_PAIR(4));

		}
		refresh();
		c++;
		nanosleep((const struct timespec[]){{0, 100000000L}}, NULL);
	}

	PQfinish(conn);
	endwin();

	printf("FINISHED\n");

	return 0;
};
