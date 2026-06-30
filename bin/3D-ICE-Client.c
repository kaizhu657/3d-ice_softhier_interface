/******************************************************************************
 * This file is part of 3D-ICE, version 4.0 .                                 *
 *                                                                            *
 * 3D-ICE is free software: you can  redistribute it and/or  modify it  under *
 * the terms of the  GNU General  Public  License as  published by  the  Free *
 * Software  Foundation, either  version  3  of  the License,  or  any  later *
 * version.                                                                   *
 *                                                                            *
 * 3D-ICE is  distributed  in the hope  that it will  be useful, but  WITHOUT *
 * ANY  WARRANTY; without  even the  implied warranty  of MERCHANTABILITY  or *
 * FITNESS  FOR A PARTICULAR  PURPOSE. See the GNU General Public License for *
 * more details.                                                              *
 *                                                                            *
 * You should have  received a copy of  the GNU General  Public License along *
 * with 3D-ICE. If not, see <http://www.gnu.org/licenses/>.                   *
 *                                                                            *
 *                             Copyright (C) 2021                             *
 *   Embedded Systems Laboratory - Ecole Polytechnique Federale de Lausanne   *
 *                            All Rights Reserved.                            *
 *                                                                            *
 * Authors: Arvind Sridhar              Alessandro Vincenzi                   *
 *          Giseong Bak                 Martino Ruggiero                      *
 *          Thomas Brunschwiler         Eder Zulian                           *
 *          Federico Terraneo           Darong Huang                          *
 *          Kai Zhu                     Luis Costero                          *
 *          Marina Zapater              David Atienza                         *
 *                                                                            *
 * For any comment, suggestion or request  about 3D-ICE, please  register and *
 * write to the mailing list (see http://listes.epfl.ch/doc.cgi?liste=3d-ice) *
 * Any usage  of 3D-ICE  for research,  commercial or other  purposes must be *
 * properly acknowledged in the resulting products or publications.           *
 *                                                                            *
 * EPFL-STI-IEL-ESL                     Mail : 3d-ice@listes.epfl.ch          *
 * Batiment ELG, ELG 130                       (SUBSCRIPTION IS NECESSARY)    *
 * Station 11                                                                 *
 * 1015 Lausanne, Switzerland           Url  : http://esl.epfl.ch/3d-ice      *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "network_socket.h"
#include "network_message.h"

void seed_random ()
{
    FILE *fp = fopen ("/dev/urandom", "r");
    unsigned int foo;
    struct timeval t;

    if (!fp)
    {
        gettimeofday (&t, NULL);
        foo = t.tv_usec;
    }
    else
    {
        size_t res = fread (&foo, sizeof (foo), 1, fp);
        if (res == 0 ) printf ("fread failed\n");
        fclose (fp);
    }
    srand (foo);
}

#define random_value(min_value, max_value) \
    (   ((double)(min_value))              \
      + ( ( (double)(max_value)-((double)(min_value)) )*rand() / (RAND_MAX+1.0f)) )

#define MAX_SERVER_IP 50
#define TERMINATION_SENTINEL (-1.0f)

static void print_usage (char *exe_name)
{
    fprintf (stderr,
        "Usage: \"%s nslots server_ip server_port [power_trace_file [--follow] [--until-minus-one]]\"\n",
        exe_name) ;
    fprintf (stderr,
        "       \"%s server_ip server_port power_trace_file [--follow] --until-minus-one\"\n",
        exe_name) ;
}

static int is_non_negative_integer (char *value)
{
    if (*value == '\0')

        return 0 ;

    for ( ; *value != '\0' ; value++)
    {
        if (*value < '0' || *value > '9')

            return 0 ;
    }

    return 1 ;
}

static Error_t read_power_trace_value
(
    FILE       *power_trace,
    int         follow_power_trace,
    Quantity_t slot_index,
    Quantity_t value_index,
    float      *power
)
{
    int waiting = 0 ;

    for ( ; ; )
    {
        int result = fscanf (power_trace, "%f", power) ;

        if (result == 1)

            return TDICE_SUCCESS ;

        if (result == EOF && follow_power_trace != 0)
        {
            if (waiting == 0)
            {
                fprintf (stdout,
                    "Waiting for power trace values for slot %u, element %u ...\n",
                    (unsigned int) slot_index,
                    (unsigned int) value_index) ;
                fflush (stdout) ;

                waiting = 1 ;
            }

            clearerr (power_trace) ;
            sleep (1u) ;

            continue ;
        }

        fprintf (stderr,
            "Cannot read power trace value for slot %u, element %u\n",
            (unsigned int) slot_index,
            (unsigned int) value_index) ;

        return TDICE_FAILURE ;
    }
}

int main (int argc, char** argv)
{
    Socket_t client_socket ;

    NetworkMessage_t client_nflp, client_powers, client_simulate, client_close_sim, server_reply ;

    Quantity_t nflpel, index, nslots, server_port, slot_index ;

    char server_ip [MAX_SERVER_IP] ;

    SimResult_t sim_result ;
    Error_t error ;

    float power ;

    FILE *power_trace ;

    int follow_power_trace, terminate_on_sentinel, unlimited_slots, nslots_provided ;
    int option_start_index ;
    long requested_slots ;
    char *power_trace_file ;

    seed_random () ;

    /* Checks if all arguments are there **************************************/

#define NARGC_MIN    4
#define NARGC_MAX    7
#define EXE_NAME     argv[0]
#define NSLOTS       argv[1]
#define SERVER_IP    argv[2]
#define SERVER_PORT  argv[3]
#define POWER_TRACE  argv[4]

    if (argc < NARGC_MIN || argc > NARGC_MAX)
    {
        print_usage (EXE_NAME) ;

        return EXIT_FAILURE ;
    }

    power_trace = NULL ;
    power_trace_file = NULL ;
    follow_power_trace = 0 ;
    terminate_on_sentinel = 0 ;
    nslots_provided = is_non_negative_integer (NSLOTS) ;

    if (nslots_provided != 0)
    {
        requested_slots = atol (NSLOTS) ;
        nslots = (Quantity_t) requested_slots ;

        if (strlen (SERVER_IP) > MAX_SERVER_IP - 1)
        {
            fprintf (stderr, "Server ip %s too long !!!\n", SERVER_IP) ;

            return EXIT_FAILURE ;
        }

        strcpy (server_ip, SERVER_IP) ;

        server_port = atoi (SERVER_PORT) ;

        option_start_index = 4 ;

        if (argc >= 5)
        {
            power_trace_file = POWER_TRACE ;
            option_start_index = 5 ;
        }
    }
    else
    {
        if (argc < 5 || argc > 6)
        {
            print_usage (EXE_NAME) ;

            return EXIT_FAILURE ;
        }

        nslots = (Quantity_t) 0u ;

        if (strlen (NSLOTS) > MAX_SERVER_IP - 1)
        {
            fprintf (stderr, "Server ip %s too long !!!\n", NSLOTS) ;

            return EXIT_FAILURE ;
        }

        strcpy (server_ip, NSLOTS) ;

        server_port = atoi (SERVER_IP) ;
        power_trace_file = SERVER_PORT ;
        option_start_index = 4 ;
    }

    if (power_trace_file != NULL)
    {
        power_trace = fopen (power_trace_file, "r") ;

        if (power_trace == NULL)
        {
            fprintf (stderr, "Cannot open power trace file %s\n", power_trace_file) ;

            return EXIT_FAILURE ;
        }
    }

    for (int arg_index = option_start_index ; arg_index < argc ; arg_index++)
    {
        if (strcmp (argv [arg_index], "--follow") == 0)
        {
            follow_power_trace = 1 ;
        }
        else if (strcmp (argv [arg_index], "--until-minus-one") == 0)
        {
            terminate_on_sentinel = 1 ;
        }
        else
        {
            fprintf (stderr, "Unknown power trace mode %s\n", argv [arg_index]) ;
            print_usage (EXE_NAME) ;

            if (power_trace != NULL)

                fclose (power_trace) ;

            return EXIT_FAILURE ;
        }
    }

    if (terminate_on_sentinel != 0 && power_trace == NULL)
    {
        fprintf (stderr, "--until-minus-one requires a power trace file\n") ;

        return EXIT_FAILURE ;
    }

    if (nslots == 0u && terminate_on_sentinel == 0)
    {
        fprintf (stderr, "nslots must be positive unless --until-minus-one is used, or omitted with --until-minus-one\n") ;

        if (power_trace != NULL)

            fclose (power_trace) ;

        return EXIT_FAILURE ;
    }

    unlimited_slots = (nslots == 0u && terminate_on_sentinel != 0) ;

    /* Creates socket *********************************************************/

    fprintf (stdout, "Creating socket ... ") ; fflush (stdout) ;

    socket_init (&client_socket) ;

    if (open_client_socket (&client_socket) != TDICE_SUCCESS)

        return EXIT_FAILURE ;

    fprintf (stdout, "done !\n") ;

    /* Connect to the server **************************************************/

    fprintf (stdout, "Connecting to server ... ") ; fflush (stdout) ;

    if (connect_client_to_server (&client_socket, (String_t)server_ip, server_port) != TDICE_SUCCESS)

        return EXIT_FAILURE ;

    fprintf (stdout, "done !\n") ;

    /* Client-Server Communication ********************************************/
    /**************************************************************************/

    network_message_init (&client_nflp) ;
    build_message_head   (&client_nflp, TDICE_TOTAL_NUMBER_OF_FLOORPLAN_ELEMENTS) ;

    send_message_to_socket      (&client_socket, &client_nflp) ;
    receive_message_from_socket (&client_socket, &client_nflp) ;

    extract_message_word (&client_nflp, &nflpel, 0) ;

    network_message_destroy (&client_nflp) ;

    for (slot_index = 0u ; unlimited_slots != 0 || nslots != 0u ; slot_index++)
    {
        int termination_slot = (terminate_on_sentinel != 0) ;

        /* client sends power values ******************************************/

        network_message_init (&client_powers) ;
        build_message_head   (&client_powers, TDICE_INSERT_POWERS) ;
        insert_message_word  (&client_powers, &nflpel) ;

        for (index = 0 ; index != nflpel ; index++)
        {
            if (power_trace != NULL)
            {
                if (read_power_trace_value
                    (power_trace, follow_power_trace, slot_index, index, &power) != TDICE_SUCCESS)
                {
                    network_message_destroy (&client_powers) ;

                    fclose (power_trace) ;
                    socket_close (&client_socket) ;

                    return EXIT_FAILURE ;
                }
            }
            else
            {
                power = random_value (0.0, 1.0) ;
            }

            if (termination_slot != 0 && power != TERMINATION_SENTINEL)

                termination_slot = 0 ;

            insert_message_word (&client_powers, &power) ;
        }

        if (termination_slot != 0)
        {
            fprintf (stdout,
                "Received all-minus-one termination slot at slot %u; stopping simulation.\n",
                (unsigned int) slot_index) ;

            network_message_destroy (&client_powers) ;

            break ;
        }

        send_message_to_socket (&client_socket, &client_powers) ;

        network_message_destroy (&client_powers) ;

        /* Client waits for power insertion result ****************************/

        network_message_init (&server_reply) ;

        receive_message_from_socket (&client_socket, &server_reply) ;

        extract_message_word (&server_reply, &error, 0) ;

        if (error != TDICE_SUCCESS)
        {
            network_message_destroy (&server_reply) ;

            if (power_trace != NULL)
                fclose (power_trace) ;

            socket_close (&client_socket) ;

            return EXIT_FAILURE ;
        }

        network_message_destroy (&server_reply) ;

        /* client sends request for slot simlation ****************************/

        network_message_init (&client_simulate) ;
        build_message_head   (&client_simulate, TDICE_SIMULATE_SLOT) ;

        send_message_to_socket (&client_socket, &client_simulate) ;

        network_message_destroy (&client_simulate) ;

        /* Client waits for simulation result *********************************/

        network_message_init (&server_reply) ;

        receive_message_from_socket (&client_socket, &server_reply) ;

        extract_message_word (&server_reply, &sim_result, 0) ;

        if (sim_result != TDICE_SLOT_DONE)
        {
            network_message_destroy (&server_reply) ;

            if (power_trace != NULL)
                fclose (power_trace) ;

            socket_close (&client_socket) ;

            return EXIT_FAILURE ;
        }

        network_message_destroy (&server_reply) ;

        fprintf (stdout, "Slot %u simulated.\n", (unsigned int) slot_index) ;

        if (unlimited_slots == 0)

            nslots-- ;
    }

    if (power_trace != NULL)

        fclose (power_trace) ;

    /* Closes the simulation on the server ************************************/

    network_message_init (&client_close_sim) ;
    build_message_head   (&client_close_sim, TDICE_EXIT_SIMULATION) ;

    send_message_to_socket (&client_socket, &client_close_sim) ;

    network_message_destroy (&client_close_sim) ;

    /* Closes client sockek ***************************************************/

    if (socket_close (&client_socket) != TDICE_SUCCESS)

        return EXIT_FAILURE ;

    return EXIT_SUCCESS ;
}
