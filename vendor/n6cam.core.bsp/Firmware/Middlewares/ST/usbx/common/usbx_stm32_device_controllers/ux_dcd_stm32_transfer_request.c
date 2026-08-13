/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** USBX Component                                                        */
/**                                                                       */
/**   STM32 Controller Driver                                             */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

#define UX_SOURCE_CODE
#define UX_DCD_STM32_SOURCE_CODE


/* Include necessary system files.  */

#include "ux_api.h"
#include "ux_dcd_stm32.h"
#include "ux_utility.h"
#include "ux_device_stack.h"


#if !defined(UX_DEVICE_STANDALONE)
/**************************************************************************/
/*                                                                        */
/*  FUNCTION                                               RELEASE        */
/*                                                                        */
/*    _ux_dcd_stm32_transfer_request                      PORTABLE C      */
/*                                                           6.1          */
/*  AUTHOR                                                                */
/*                                                                        */
/*    Chaoqiong Xiao, Microsoft Corporation                               */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This function will initiate a transfer to a specific endpoint.      */
/*    If the endpoint is IN, the endpoint register will be set to accept  */
/*    the request.                                                        */
/*                                                                        */
/*    If the endpoint is IN, the endpoint FIFO will be filled with the    */
/*    buffer and the endpoint register set.                               */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    dcd_stm32                             Pointer to device controller  */
/*    transfer_request                      Pointer to transfer request   */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    Completion Status                                                   */
/*                                                                        */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    HAL_PCD_EP_Transmit                   Transmit data                 */
/*    HAL_PCD_EP_Receive                    Receive data                  */
/*    _ux_utility_semaphore_get             Get semaphore                 */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    STM32 Controller Driver                                             */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  05-19-2020     Chaoqiong Xiao           Initial Version 6.0           */
/*  09-30-2020     Chaoqiong Xiao           Modified comment(s), used ST  */
/*                                            HAL library to drive the    */
/*                                            controller,                 */
/*                                            resulting in version 6.1    */
/*                                                                        */
/**************************************************************************/
UINT  _ux_dcd_stm32_transfer_request(UX_DCD_STM32 *dcd_stm32, UX_SLAVE_TRANSFER *transfer_request)
{

UX_SLAVE_ENDPOINT       *endpoint;
UINT                    status;


    /* Get the pointer to the logical endpoint from the transfer request.  */
    endpoint =  transfer_request -> ux_slave_transfer_request_endpoint;

    /* Check for transfer direction.  Is this a IN endpoint ? */
    if (transfer_request -> ux_slave_transfer_request_phase == UX_TRANSFER_PHASE_DATA_OUT)
    {

        /* Transmit data.  */
        HAL_PCD_EP_Transmit(dcd_stm32 -> pcd_handle,
                            endpoint->ux_slave_endpoint_descriptor.bEndpointAddress,
                            transfer_request->ux_slave_transfer_request_data_pointer,
                            transfer_request->ux_slave_transfer_request_requested_length);

        /* If the endpoint is a Control endpoint, all this is happening under Interrupt and there is no
           thread to suspend.  */
        if ((endpoint -> ux_slave_endpoint_descriptor.bEndpointAddress & (UINT)~UX_ENDPOINT_DIRECTION) != 0)
        {

            /* We should wait for the semaphore to wake us up.  */
            status =  _ux_utility_semaphore_get(&transfer_request -> ux_slave_transfer_request_semaphore,
                                                (ULONG)transfer_request -> ux_slave_transfer_request_timeout);

            /* Check the completion code. */
            if (status != UX_SUCCESS)
            {

                /* KAMACODE FIX: a timeout here used to return with the
                   hardware transfer still armed, which poisoned the endpoint
                   for the rest of the boot.

                   HAL_PCD_EP_Transmit has already handed the buffer to the
                   peripheral. Returning without taking it back leaves the
                   endpoint busy for ever if the host never drains it: every
                   later HAL_PCD_EP_Transmit on the same address is refused,
                   so nothing is sent, so the semaphore is never posted, so
                   the next caller times out too. One expired write kills the
                   direction permanently.

                   On this product that is not a corner case. The device
                   writes to its CDC console on its own — a detection
                   notification every time the scene changes — and a QA
                   terminal is not always attached to read it. The first
                   notification written with nobody listening killed the
                   console, and from then on commands were received and
                   executed with their replies going nowhere. It looks
                   exactly like a hung device and is not one.

                   So take the transfer back before returning: abort and
                   flush the endpoint, then drain any post the ISR may have
                   slipped in while we were doing it, so the next caller
                   starts from a clean endpoint and a zero semaphore rather
                   than inheriting a stale completion. The caller still sees
                   the timeout and still decides what to do about it — the
                   only thing that changes is that the next write works.  */
                _ux_dcd_stm32_transfer_abort(dcd_stm32, transfer_request);
                (void)_ux_utility_semaphore_get(&transfer_request -> ux_slave_transfer_request_semaphore, 0);

                return(status);
            }

            transfer_request -> ux_slave_transfer_request_actual_length = transfer_request->ux_slave_transfer_request_requested_length;

            /* Check the transfer request completion code. We may have had a BUS reset or
               a device disconnection.  */
            if (transfer_request -> ux_slave_transfer_request_completion_code != UX_SUCCESS)
                return(transfer_request -> ux_slave_transfer_request_completion_code);

            /* Return to caller with success.  */
            return(UX_SUCCESS);
        }
    }
    else
    {

        /* We have a request for a SETUP or OUT Endpoint.  */
        /* Receive data.  */
        HAL_PCD_EP_Receive(dcd_stm32 -> pcd_handle,
                            endpoint->ux_slave_endpoint_descriptor.bEndpointAddress,
                            transfer_request->ux_slave_transfer_request_data_pointer,
                            transfer_request->ux_slave_transfer_request_requested_length);

        /* If the endpoint is a Control endpoint, all this is happening under Interrupt and there is no
           thread to suspend.  */
        if ((endpoint -> ux_slave_endpoint_descriptor.bEndpointAddress & (UINT)~UX_ENDPOINT_DIRECTION) != 0)
        {

            /* We should wait for the semaphore to wake us up.  */
            status =  _ux_utility_semaphore_get(&transfer_request -> ux_slave_transfer_request_semaphore,
                                                (ULONG)transfer_request -> ux_slave_transfer_request_timeout);

            /* Check the completion code. */
            /* NOTE: deliberately NOT aborting the transfer on a timeout here,
               unlike the DATA_OUT path above. The receive direction shows no
               sign of the same fault — the shell reads with a timeout on every
               pass of its loop and RX keeps working for days — and aborting
               would mean flushing the endpoint, which discards a byte that
               arrived in the instant between the timeout expiring and the
               abort. Re-arming a still-armed OUT endpoint is a latent version
               of the same bug, but it has never been observed, and trading a
               working channel for a theoretical fix is a bad bargain.  */
            if (status != UX_SUCCESS)
                return(status);

            /* Check the transfer request completion code. We may have had a BUS reset or
               a device disconnection.  */
            if (transfer_request -> ux_slave_transfer_request_completion_code != UX_SUCCESS)
                return(transfer_request -> ux_slave_transfer_request_completion_code);

            /* Return to caller with success.  */
            return(UX_SUCCESS);
        }
    }

    /* Return to caller with success.  */
    return(UX_SUCCESS);
}
#endif
