resolvers += MavenCache("local-maven", Path.userHome / ".m2" / "repository")
resolvers += "YandexMediaReleases" at "http://artifactory.yandex.net/artifactory/yandex_media_releases"

addSbtPlugin("com.eed3si9n" % "sbt-assembly" % "0.14.10")

addSbtPlugin("net.virtual-void" % "sbt-dependency-graph" % "0.10.0-RC1")

addSbtPlugin("com.typesafe.sbt" % "sbt-native-packager" % "1.4.1")

addSbtPlugin("com.eed3si9n" % "sbt-buildinfo" % "0.9.0")

libraryDependencies += "org.vafer" % "jdeb" % "1.3" artifacts (Artifact("jdeb", "jar", "jar"))
libraryDependencies += "com.github.eikek" %% "yamusca-core" % "0.5.1"

val circeVersion = "0.12.3"
libraryDependencies ++= Seq(
  "io.circe" %% "circe-core",
  "io.circe" %% "circe-generic",
  "io.circe" %% "circe-parser"
).map(_ % circeVersion)

val yandexIcebergVersion = "7150580"
libraryDependencies ++= Seq(
  "ru.yandex" % "iceberg-inside-yt" % yandexIcebergVersion excludeAll (
    ExclusionRule(organization = "com.fasterxml.jackson.core")
  )
)