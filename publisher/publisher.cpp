/*
 * Copyright 2019-2020 Cypress Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @file
 *
 * Reference code example for AWS Greengrass publisher
 */
#include "mbed.h"
#include "aws_client.h"
#include "aws_config.h"

NetworkInterface* network;
aws_endpoint_params_t endpoint_params;

#define APP_INFO( x )                   printf x

#define AWSIOT_KEEPALIVE_TIMEOUT        (60)
#define AWSIOT_MESSAGE                  "HELLO"
#define AWS_IOT_SECURE_PORT             (8883)
#define AWSIOT_TIMEOUT_IN_USEC          (1000UL * 1000UL)

static void my_publisher_greengrass_discovery_callback( aws_greengrass_discovery_callback_data_t* data )
{
    cy_linked_list_t* groups_list = NULL;
    cy_linked_list_node_t* node = NULL;
    aws_greengrass_core_info_t* info = NULL;
    aws_greengrass_core_connection_info_t* connection = NULL;

    /* Fill 'my_publisher_greengrass_core_endpoint' with a Group information fetched from group-list.
     * Application may use some filters( metadata/core-name etc.) to select which core it wants
     * to connect to. Right now, we are just using the first core(and its first 'Connection' field)
     * from the list.
     */
    groups_list = data->groups;

    if( !groups_list || !groups_list->count )
    {
        APP_INFO (( "[Application/AWS] Greengrass discovery Payload is empty\n" ));
        return;
    }

    cy_linked_list_get_front_node( groups_list, &node );
    if( !node )
    {
        APP_INFO (( "[Application/AWS] Greengrass discovery - Node not found\n" ));
        return;
    }

    info = &(( aws_greengrass_core_t *)node->data)->info;

    APP_INFO (( " ==== Core/Group Information ====\n" ));
    APP_INFO (( "%s: %s\n", GG_GROUP_ID,           info->group_id ));
    APP_INFO (( "%s: %s\n", GG_CORE_THING_ARN,     info->thing_arn ));
    APP_INFO (( "%s: %s\n", GG_ROOT_CAS,           info->root_ca_certificate ));
    APP_INFO (( " ==== End of Core/Group Information ====\n" ));

    cy_linked_list_get_front_node( &info->connections, &node );
    if( !node )
    {
        APP_INFO (( "[Application/AWS] Greengrass discovery - Connections not found\n" ));
        return;
    }

    /* Set-up the Connection parameters */
    connection = &(( aws_greengrass_core_connection_t *)node->data)->info;

    /* set MQTT endpoint parameters */
    endpoint_params.transport = AWS_TRANSPORT_MQTT_NATIVE;
    endpoint_params.uri = connection->ip_address;
    endpoint_params.port = atoi(connection->port);
    endpoint_params.root_ca = info->root_ca_certificate;
    endpoint_params.root_ca_length = strlen(info->root_ca_certificate);

    return;
}

int main( void )
{
    aws_connect_params_t conn_params = { 0, 0, NULL, NULL, NULL, NULL, NULL };
    aws_publish_params_t publish_params = { AWS_QOS_ATMOST_ONCE };
    cy_rslt_t result = CY_RSLT_SUCCESS;
    endpoint_params = { AWS_TRANSPORT_MQTT_NATIVE, NULL, 0, NULL, 0 };
    AWSIoTClient *client = NULL;
    SocketAddress address;

    APP_INFO (( "Connecting to the network using Wifi...\r\n" ));
    network = NetworkInterface::get_default_instance();

    nsapi_error_t net_status = -1;
    for ( int tries = 0; tries < 3; tries++ )
    {
        net_status = network->connect();
        if ( net_status == NSAPI_ERROR_OK )
        {
            break;
        }
        else
        {
            APP_INFO (( "Unable to connect to network. Retrying...\r\n" ));
        }
    }

    if ( net_status != NSAPI_ERROR_OK )
    {
        APP_INFO (( "ERROR: Connecting to the network failed (%d)!\r\n", net_status ));
        return -1;
    }

    network->get_ip_address(&address);
    APP_INFO(( "Connected to the network successfully. IP address: %s\n", address.get_ip_address() ));

    if ( ( strlen(SSL_CLIENTKEY_PEM) | strlen(SSL_CLIENTCERT_PEM) | strlen(SSL_CA_PEM) ) < 64 )
    {
        APP_INFO(( "Please configure SSL_CLIENTKEY_PEM, SSL_CLIENTCERT_PEM and SSL_CA_PEM in aws_config.h file\n" ));
        return -1;
    }

    /* Initialize AWS Client library */
    client = new AWSIoTClient( network, AWSIOT_THING_NAME, SSL_CLIENTKEY_PEM, strlen(SSL_CLIENTKEY_PEM), SSL_CLIENTCERT_PEM, strlen(SSL_CLIENTCERT_PEM) );

    result = client->discover( AWS_TRANSPORT_MQTT_NATIVE, AWSIOT_ENDPOINT_ADDRESS, SSL_CA_PEM, strlen(SSL_CA_PEM), my_publisher_greengrass_discovery_callback );
    if ( result != CY_RSLT_SUCCESS )
    {
        APP_INFO (( "Error in discovering node info \n" ));
        if( client != NULL )
        {
            delete client;
            client = NULL;
        }
        return 1;
    }

    APP_INFO (( "Discovery of Greengrass Core successful\n" ));

    wait_us( AWSIOT_TIMEOUT_IN_USEC * 1 );

    /* set MQTT connection parameters */
    conn_params.username = NULL;
    conn_params.password = NULL;
    conn_params.keep_alive = AWSIOT_KEEPALIVE_TIMEOUT;
    conn_params.peer_cn = NULL;
    conn_params.client_id = (uint8_t*)AWSIOT_THING_NAME;

    /* connect to an AWS endpoint */
    result = client->connect( conn_params, endpoint_params );
    if ( result != CY_RSLT_SUCCESS )
    {
        APP_INFO(( "connection to AWS endpoint failed\r\n" ));
        if( client != NULL )
        {
            delete client;
            client = NULL;
        }
        return 1;
    }

    APP_INFO(( "Connected to AWS endpoint\r\n" ));

    wait_us( AWSIOT_TIMEOUT_IN_USEC * 1 );

    while ( 1 )
    {
        publish_params.QoS = AWS_QOS_ATMOST_ONCE;
        result = client->publish( AWSIOT_TOPIC, AWSIOT_MESSAGE, strlen((char*)AWSIOT_MESSAGE), publish_params );
        if (result != CY_RSLT_SUCCESS) {
            APP_INFO(( "publish to topic failed\r\n" ));
            if( client != NULL )
            {
                delete client;
                client = NULL;
            }
            return 1;
        }

        APP_INFO(( "Published to topic successfully\r\n" ));

        wait_us( AWSIOT_TIMEOUT_IN_USEC * 5 );
    }
}
