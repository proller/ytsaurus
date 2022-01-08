#pragma once

#include "private.h"
#include "dynamic_state.h"

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NQueueAgent {

////////////////////////////////////////////////////////////////////////////////

struct IQueueController
    : public TRefCounted
{
public:
    /*!
     *  \note Thread affinity: any.
     */
    virtual EQueueType GetQueueType() const = 0;

    /*!
     *  \note Thread affinity: any.
     */
    virtual void Start() = 0;
    /*!
     *  \note Thread affinity: any.
     */
    virtual TFuture<void> Stop() = 0;

    /*!
     *  \note Thread affinity: any.
     */
    virtual IInvokerPtr GetInvoker() const = 0;

    /*!
     *  \note Thread affinity: controller invoker.
     */
    virtual void BuildOrchid(NYTree::TFluentMap fluent) const = 0;
    /*!
     *  \note Thread affinity: controller invoker.
     */
    virtual void BuildConsumerOrchid(const TCrossClusterReference& consumerRef, NYTree::TFluentMap fluent) const = 0;
};

DEFINE_REFCOUNTED_TYPE(IQueueController)

////////////////////////////////////////////////////////////////////////////////

IQueueControllerPtr CreateQueueController(
    TCrossClusterReference queueRef,
    EQueueType queueType,
    TQueueTableRow queueRow,
    THashMap<TCrossClusterReference, TConsumerTableRow> consumerRefToRow,
    IInvokerPtr invoker);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueAgent
