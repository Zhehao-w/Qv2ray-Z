#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Qv2ray::core::connection::routing_json
{
    inline bool ManagedFieldUnchanged(const QJsonObject &baseline, const QJsonObject &current, const QString &key)
    {
        return baseline.contains(key) == current.contains(key) && baseline.value(key) == current.value(key);
    }

    inline QJsonObject MergeManagedRoutingRule(const QJsonObject &original, const QJsonObject &baseline, const QJsonObject &current)
    {
        static const QStringList managedKeys{
            QStringLiteral("type"),          QStringLiteral("outboundTag"),       QStringLiteral("balancerTag"),
            QStringLiteral("QV2RAY_RULE_ENABLED"), QStringLiteral("QV2RAY_RULE_TAG"), QStringLiteral("domain"),
            QStringLiteral("ip"),            QStringLiteral("port"),              QStringLiteral("sourcePort"),
            QStringLiteral("network"),       QStringLiteral("source"),            QStringLiteral("inboundTag"),
            QStringLiteral("protocol"),      QStringLiteral("attrs"),
        };

        auto merged = original;
        for (const auto &key : managedKeys)
        {
            if (ManagedFieldUnchanged(baseline, current, key))
                continue;
            if (current.contains(key))
                merged.insert(key, current.value(key));
            else
                merged.remove(key);
        }

        const auto endpointChanged = !ManagedFieldUnchanged(baseline, current, QStringLiteral("outboundTag")) ||
                                     !ManagedFieldUnchanged(baseline, current, QStringLiteral("balancerTag"));
        if (endpointChanged)
        {
            const auto outbound = current.value(QStringLiteral("outboundTag")).toString();
            const auto balancer = current.value(QStringLiteral("balancerTag")).toString();
            if (!outbound.isEmpty())
            {
                merged.insert(QStringLiteral("outboundTag"), outbound);
                merged.remove(QStringLiteral("balancerTag"));
            }
            else if (!balancer.isEmpty())
            {
                merged.insert(QStringLiteral("balancerTag"), balancer);
                merged.remove(QStringLiteral("outboundTag"));
            }
            else
            {
                merged.remove(QStringLiteral("outboundTag"));
                merged.remove(QStringLiteral("balancerTag"));
            }
        }
        return merged;
    }

    inline QJsonObject NormalizeNewRoutingRule(QJsonObject current)
    {
        const auto outbound = current.value(QStringLiteral("outboundTag")).toString();
        const auto balancer = current.value(QStringLiteral("balancerTag")).toString();
        if (!outbound.isEmpty())
        {
            current.remove(QStringLiteral("balancerTag"));
        }
        else if (!balancer.isEmpty())
        {
            current.remove(QStringLiteral("outboundTag"));
        }
        else
        {
            current.remove(QStringLiteral("outboundTag"));
            current.remove(QStringLiteral("balancerTag"));
        }
        return current;
    }

    inline QJsonObject ManagedBalancerJson(const QString &tag, const QStringList &selector, const QString &strategyType)
    {
        return QJsonObject{
            { QStringLiteral("tag"), tag },
            { QStringLiteral("selector"), QJsonArray::fromStringList(selector) },
            { QStringLiteral("strategy"), QJsonObject{ { QStringLiteral("type"), strategyType } } },
        };
    }

    inline QJsonObject MergeManagedRoutingBalancer(const QJsonObject &original, const QJsonObject &baseline, const QJsonObject &current)
    {
        auto merged = original;
        for (const auto &key : { QStringLiteral("tag"), QStringLiteral("selector") })
        {
            if (ManagedFieldUnchanged(baseline, current, key))
                continue;
            if (current.contains(key))
                merged.insert(key, current.value(key));
            else
                merged.remove(key);
        }

        const auto baselineStrategy = baseline.value(QStringLiteral("strategy")).toObject();
        const auto currentStrategy = current.value(QStringLiteral("strategy")).toObject();
        if (!ManagedFieldUnchanged(baselineStrategy, currentStrategy, QStringLiteral("type")))
        {
            auto strategy = merged.value(QStringLiteral("strategy")).toObject();
            if (currentStrategy.contains(QStringLiteral("type")))
                strategy.insert(QStringLiteral("type"), currentStrategy.value(QStringLiteral("type")));
            else
                strategy.remove(QStringLiteral("type"));
            merged.insert(QStringLiteral("strategy"), strategy);
        }
        return merged;
    }

    inline bool IsGraphicalRoutingStateSupported(const QJsonObject &routing, QString *reason = nullptr)
    {
        const auto reject = [reason](const QString &message) {
            if (reason)
                *reason = message;
            return false;
        };

        if (routing.contains(QStringLiteral("domainStrategy")) && !routing.value(QStringLiteral("domainStrategy")).isString())
            return reject(QStringLiteral("routing.domainStrategy is not a string"));

        if (routing.contains(QStringLiteral("rules")) && !routing.value(QStringLiteral("rules")).isArray())
            return reject(QStringLiteral("routing.rules is not an array"));

        for (const auto &value : routing.value(QStringLiteral("rules")).toArray())
        {
            if (!value.isObject())
                return reject(QStringLiteral("routing.rules contains a non-object entry"));
            const auto rule = value.toObject();
            if (!rule.value(QStringLiteral("outboundTag")).toString().isEmpty() && !rule.value(QStringLiteral("balancerTag")).toString().isEmpty())
                return reject(QStringLiteral("a routing rule contains both outboundTag and balancerTag"));
        }

        if (routing.contains(QStringLiteral("balancers")) && !routing.value(QStringLiteral("balancers")).isArray())
            return reject(QStringLiteral("routing.balancers is not an array"));

        for (const auto &value : routing.value(QStringLiteral("balancers")).toArray())
        {
            if (!value.isObject())
                return reject(QStringLiteral("routing.balancers contains a non-object entry"));
            const auto balancer = value.toObject();
            if (balancer.contains(QStringLiteral("selector")) && !balancer.value(QStringLiteral("selector")).isArray())
                return reject(QStringLiteral("a routing balancer selector is not an array"));
            if (balancer.contains(QStringLiteral("strategy")) && !balancer.value(QStringLiteral("strategy")).isObject())
                return reject(QStringLiteral("a routing balancer strategy is not an object"));
        }

        if (reason)
            reason->clear();
        return true;
    }

    inline QJsonObject MergeRoutingRoot(const QJsonObject &original, const QString &baselineDomainStrategy, const QString &currentDomainStrategy,
                                        const QJsonArray &rules, const QJsonArray &balancers)
    {
        auto merged = original;
        if (currentDomainStrategy != baselineDomainStrategy)
            merged.insert(QStringLiteral("domainStrategy"), currentDomainStrategy);

        if (original.value(QStringLiteral("rules")).toArray() != rules)
            merged.insert(QStringLiteral("rules"), rules);
        if (original.value(QStringLiteral("balancers")).toArray() != balancers)
            merged.insert(QStringLiteral("balancers"), balancers);
        return merged;
    }
} // namespace Qv2ray::core::connection::routing_json
