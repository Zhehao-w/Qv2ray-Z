#pragma once

#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Qv2ray::core::connection::routing_json
{
    inline bool ManagedFieldUnchanged(const QJsonObject &baseline, const QJsonObject &current, const QString &key)
    {
        return baseline.contains(key) == current.contains(key) && baseline.value(key) == current.value(key);
    }

    inline QJsonValue MergeEditedRouteSettings(const QJsonValue &original, const QJsonValue &baseline, const QJsonValue &current)
    {
        if (baseline == current)
            return original;

        if (baseline.isObject() && current.isObject())
        {
            auto result = original.toObject();
            const auto before = baseline.toObject();
            const auto after = current.toObject();
            auto keys = before.keys();
            for (const auto &key : after.keys())
                if (!keys.contains(key))
                    keys.append(key);
            for (const auto &key : keys)
                if (before.value(key) != after.value(key))
                    result[key] = MergeEditedRouteSettings(result.value(key), before.value(key), after.value(key));
            return result;
        }

        if (baseline.isArray() && current.isArray())
        {
            const auto source = original.toArray();
            const auto before = baseline.toArray();
            const auto after = current.toArray();
            QVector<int> matches(after.size(), -1);
            QVector<bool> used(before.size(), false);

            const auto bind = [&](int currentIndex, int baselineIndex) {
                matches[currentIndex] = baselineIndex;
                used[baselineIndex] = true;
            };

            const auto sharedSize = std::min(before.size(), after.size());
            for (int i = 0; i < sharedSize; ++i)
                if (before[i] == after[i])
                    bind(i, i);

            // Preserve the raw identity of unchanged elements that moved.
            for (int i = 0; i < after.size(); ++i)
            {
                if (matches[i] >= 0)
                    continue;
                for (int j = 0; j < before.size(); ++j)
                {
                    if (!used[j] && before[j] == after[i])
                    {
                        bind(i, j);
                        break;
                    }
                }
            }

            // When cardinality is unchanged, remaining pairs represent modeled edits.
            // Pair the same slot first, then any remaining one-to-one entries. This keeps
            // opaque fields attached to an edited element without flattening the array.
            if (before.size() == after.size())
            {
                for (int i = 0; i < after.size(); ++i)
                    if (matches[i] < 0 && !used[i])
                        bind(i, i);

                int nextBaseline = 0;
                for (int i = 0; i < after.size(); ++i)
                {
                    if (matches[i] >= 0)
                        continue;
                    while (nextBaseline < before.size() && used[nextBaseline])
                        ++nextBaseline;
                    if (nextBaseline < before.size())
                        bind(i, nextBaseline++);
                }
            }

            QJsonArray result;
            for (int i = 0; i < after.size(); ++i)
            {
                const auto baselineIndex = matches[i];
                if (baselineIndex >= 0 && baselineIndex < source.size())
                    result.append(MergeEditedRouteSettings(source[baselineIndex], before[baselineIndex], after[i]));
                else
                    result.append(after[i]);
            }
            return result;
        }

        return current;
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
