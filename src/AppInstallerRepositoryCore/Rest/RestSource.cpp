// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "RestSource.h"

using namespace AppInstaller::Utility;

namespace AppInstaller::Repository::Rest
{
    namespace
    {
        using namespace AppInstaller::Repository::Rest::Schema;

        // The source reference used by package objects.
        struct SourceReference
        {
            SourceReference(const std::shared_ptr<const RestSource>& source) :
                m_source(source) {}

        protected:
            std::shared_ptr<const RestSource> GetReferenceSource() const
            {
                std::shared_ptr<const RestSource> source = m_source.lock();
                THROW_HR_IF(E_NOT_VALID_STATE, !source);
                return source;
            }

        private:
            std::weak_ptr<const RestSource> m_source;
        };

        // The IPackageVersion impl for RestSource.
        struct PackageVersion : public SourceReference, public IPackageVersion
        {
            PackageVersion(
                const std::shared_ptr<const RestSource>& source, IRestClient::PackageInfo packageInfo, IRestClient::VersionInfo versionInfo)
                : SourceReference(source), m_packageInfo(std::move(packageInfo)), m_versionInfo(std::move(versionInfo)) {}

            // Inherited via IPackageVersion
            Utility::LocIndString GetProperty(PackageVersionProperty property) const override
            {
                switch (property)
                {
                case PackageVersionProperty::SourceIdentifier:
                    return Utility::LocIndString{ GetReferenceSource()->GetIdentifier() };
                case PackageVersionProperty::SourceName:
                    return Utility::LocIndString{ GetReferenceSource()->GetDetails().Name };
                case PackageVersionProperty::Id:
                    return Utility::LocIndString{ m_packageInfo.PackageIdentifier };
                case PackageVersionProperty::Name:
                    return Utility::LocIndString{ m_packageInfo.PackageName };
                case PackageVersionProperty::Version:
                    return Utility::LocIndString{ m_versionInfo.VersionAndChannel.GetVersion().ToString() };
                case PackageVersionProperty::Channel:
                    return Utility::LocIndString{ m_versionInfo.VersionAndChannel.GetChannel().ToString() };
                default:
                    return Utility::LocIndString{};
                }
            }

            std::vector<Utility::LocIndString> GetMultiProperty(PackageVersionMultiProperty property) const override
            {
                std::vector<Utility::LocIndString> result;
                switch (property)
                {
                case PackageVersionMultiProperty::PackageFamilyName:
                    for (std::string pfn : m_versionInfo.PackageFamilyNames)
                    {
                        result.emplace_back(Utility::LocIndString{ pfn });
                    }
                    break;
                case PackageVersionMultiProperty::ProductCode:
                    for (std::string productCode : m_versionInfo.ProductCodes)
                    {
                        result.emplace_back(Utility::LocIndString{ productCode });
                    }
                    break;
                case PackageVersionMultiProperty::Name:
                    if (m_versionInfo.Manifest)
                    {
                        BuildPackageVersionMultiPropertyWithFallback<AppInstaller::Manifest::Localization::PackageName>(result);
                    }
                    else
                    {
                        result.emplace_back(m_packageInfo.PackageName);
                    }
                    break;
                case PackageVersionMultiProperty::Publisher:
                    if (m_versionInfo.Manifest)
                    {
                        BuildPackageVersionMultiPropertyWithFallback<AppInstaller::Manifest::Localization::Publisher>(result);
                    }
                    else
                    {
                        result.emplace_back(m_packageInfo.Publisher);
                    }
                    break;
                case PackageVersionMultiProperty::Locale:
                    if (m_versionInfo.Manifest)
                    {
                        result.emplace_back(m_versionInfo.Manifest->DefaultLocalization.Locale);
                        for (const auto& loc : m_versionInfo.Manifest->Localizations)
                        {
                            result.emplace_back(loc.Locale);
                        }
                    }
                    break;
                }

                return result;
            }

            Manifest::Manifest GetManifest() override
            {
                AICLI_LOG(Repo, Verbose, << "Getting manifest");

                if (m_versionInfo.Manifest)
                {
                    return m_versionInfo.Manifest.value();
                }

                std::optional<Manifest::Manifest> manifest = GetReferenceSource()->GetRestClient().GetManifestByVersion(
                    m_packageInfo.PackageIdentifier, m_versionInfo.VersionAndChannel.GetVersion().ToString(), m_versionInfo.VersionAndChannel.GetChannel().ToString());

                if (!manifest)
                {
                    AICLI_LOG(Repo, Verbose, << "Valid manifest not found for package: " << m_packageInfo.PackageIdentifier);
                    return {};
                }
                
                m_versionInfo.Manifest = std::move(manifest.value());
                return m_versionInfo.Manifest.value();
            }

            std::shared_ptr<const ISource> GetSource() const override
            {
                return GetReferenceSource();
            }

            IPackageVersion::Metadata GetMetadata() const override
            {
                IPackageVersion::Metadata result;
                return result;
            }

        private:
            template<AppInstaller::Manifest::Localization Field>
            void BuildPackageVersionMultiPropertyWithFallback(std::vector<Utility::LocIndString>& result) const
            {
                result.emplace_back(m_versionInfo.Manifest->DefaultLocalization.Get<Field>());
                for (const auto& loc : m_versionInfo.Manifest->Localizations)
                {
                    auto f = loc.Get<Field>();
                    if (f.empty())
                    {
                        result.emplace_back(loc.Get<Field>());
                    }
                    else
                    {
                        result.emplace_back(std::move(f));
                    }
                }
            }

            IRestClient::PackageInfo m_packageInfo;
            IRestClient::VersionInfo m_versionInfo;
        };

        // The base for IPackage implementations here.
        struct PackageBase : public SourceReference
        {
            PackageBase(const std::shared_ptr<const RestSource>& source, IRestClient::Package&& package) :
                SourceReference(source), m_package(std::move(package))
            {
                 // Sort the versions
                 std::sort(m_package.Versions.begin(), m_package.Versions.end(),
                     [](const IRestClient::VersionInfo& a, const IRestClient::VersionInfo& b)
                     {
                         return a.VersionAndChannel < b.VersionAndChannel;
                     });
            }

            Utility::LocIndString GetProperty(PackageProperty property) const
            {
                switch (property)
                {
                case PackageProperty::Id:
                    return Utility::LocIndString{ m_package.PackageInformation.PackageIdentifier };
                case PackageProperty::Name:
                    return Utility::LocIndString{ m_package.PackageInformation.PackageName };
                default:
                    THROW_HR(E_UNEXPECTED);
                }
            }

        protected:
            std::shared_ptr<IPackageVersion> GetLatestVersionInternal() const
            {
                IRestClient::VersionInfo latestVersion = m_package.Versions.front();
                return std::make_shared<PackageVersion>(GetReferenceSource(), m_package.PackageInformation, latestVersion);
            }

            IRestClient::Package m_package;
        };

        // The IPackage impl for Available packages from RestSource.
        struct AvailablePackage : public PackageBase, public IPackage
        {
            using PackageBase::PackageBase;

            // Inherited via IPackage
            Utility::LocIndString GetProperty(PackageProperty property) const override
            {
                return PackageBase::GetProperty(property);
            }

            std::shared_ptr<IPackageVersion> GetInstalledVersion() const override
            {
                return {};
            }

            std::vector<PackageVersionKey> GetAvailableVersionKeys() const override
            {
                std::shared_ptr<const RestSource> source = GetReferenceSource();

                std::vector<PackageVersionKey> result;
                for (const auto& versionInfo : m_package.Versions)
                {
                    result.emplace_back(
                        source->GetIdentifier(), versionInfo.VersionAndChannel.GetVersion().ToString(), versionInfo.VersionAndChannel.GetChannel().ToString());
                }

                return result;
            }

            std::shared_ptr<IPackageVersion> GetLatestAvailableVersion() const override
            {
                return GetLatestVersionInternal();
            }

            std::shared_ptr<IPackageVersion> GetAvailableVersion(const PackageVersionKey& versionKey) const override
            {
                std::shared_ptr<const RestSource> source = GetReferenceSource();

                // Ensure that this key targets this (or any) source
                if (!versionKey.SourceId.empty() && versionKey.SourceId != source->GetIdentifier())
                {
                    return {};
                }

                std::shared_ptr<IPackageVersion> packageVersion;
                if (!versionKey.Version.empty() && !versionKey.Channel.empty())
                {
                    for (const auto& versionInfo : m_package.Versions)
                    {
                        if (CaseInsensitiveEquals(versionInfo.VersionAndChannel.GetVersion().ToString(), versionKey.Version)
                            && CaseInsensitiveEquals(versionInfo.VersionAndChannel.GetChannel().ToString(), versionKey.Channel))
                        {
                            packageVersion = std::make_shared<PackageVersion>(source, m_package.PackageInformation, versionInfo);
                            break;
                        }
                    }
                }
                else if (versionKey.Version.empty() && versionKey.Channel.empty())
                {
                    packageVersion = GetLatestAvailableVersion();
                }
                else if (versionKey.Version.empty())
                {
                    for (const auto& versionInfo : m_package.Versions)
                    {
                        if (CaseInsensitiveEquals(versionInfo.VersionAndChannel.GetChannel().ToString(), versionKey.Channel))
                        {
                            packageVersion = std::make_shared<PackageVersion>(source, m_package.PackageInformation, versionInfo);
                            break;
                        }
                    }
                }
                else if (versionKey.Channel.empty())
                {
                    for (const auto& versionInfo : m_package.Versions)
                    {
                        if (CaseInsensitiveEquals(versionInfo.VersionAndChannel.GetVersion().ToString(), versionKey.Version))
                        {
                            packageVersion = std::make_shared<PackageVersion>(source, m_package.PackageInformation, versionInfo);
                            break;
                        }
                    }
                }

                return packageVersion;
            }

            bool IsUpdateAvailable() const override
            {
                return false;
            }

            bool IsSame(const IPackage* other) const override
            {
                const AvailablePackage* otherAvailablePackage = dynamic_cast<const AvailablePackage*>(other);

                if (otherAvailablePackage)
                {
                    return GetReferenceSource()->IsSame(otherAvailablePackage->GetReferenceSource().get()) &&
                        Utility::CaseInsensitiveEquals(m_package.PackageInformation.PackageIdentifier, otherAvailablePackage->m_package.PackageInformation.PackageIdentifier);
                }

                return false;
            }
        };
    }

    RestSource::RestSource(const SourceDetails& details, std::string identifier, RestClient&& restClient)
        : m_details(details), m_restClient(std::move(restClient))
    {
        m_details.Identifier = std::move(identifier);

        const auto& sourceInformation = m_restClient.GetSourceInformation();
        m_details.Information.UnsupportedPackageMatchFields = sourceInformation.UnsupportedPackageMatchFields;
        m_details.Information.RequiredPackageMatchFields = sourceInformation.RequiredPackageMatchFields;
        m_details.Information.UnsupportedQueryParameters = sourceInformation.UnsupportedQueryParameters;
        m_details.Information.RequiredQueryParameters = sourceInformation.RequiredQueryParameters;

        m_details.Information.SourceAgreementsIdentifier = sourceInformation.SourceAgreementsIdentifier;
        for (auto const& agreement : sourceInformation.SourceAgreements)
        {
            m_details.Information.SourceAgreements.emplace_back(agreement.Label, agreement.Text, agreement.Url);
        }
    }

    const SourceDetails& RestSource::GetDetails() const
    {
        return m_details;
    }

    const RestClient& RestSource::GetRestClient() const
    {
        return m_restClient;
    }

    const std::string& RestSource::GetIdentifier() const
    {
        return m_details.Identifier;
    }

    SearchResult RestSource::Search(const SearchRequest& request) const
    {
        IRestClient::SearchResult results = m_restClient.Search(request);
        SearchResult searchResult;

        std::shared_ptr<const RestSource> sharedThis = shared_from_this();
        for (auto& result : results.Matches)
        {
            std::unique_ptr<IPackage> package = std::make_unique<AvailablePackage>(sharedThis, std::move(result));

            // TODO: Improvise to use Package match filter to return relevant search results.
            PackageMatchFilter packageFilter{ {}, {}, {} };

            searchResult.Matches.emplace_back(std::move(package), std::move(packageFilter));
        }

        searchResult.Truncated = results.Truncated;

        return searchResult;
    }

    bool RestSource::IsSame(const RestSource* other) const
    {
        return (other && GetIdentifier() == other->GetIdentifier());
    }
}
