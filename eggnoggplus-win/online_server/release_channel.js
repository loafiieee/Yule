"use strict";

function releaseChannel(env = process.env) {
  const channel = env.RELEASE_CHANNEL || "stable";
  if (channel !== "stable" && channel !== "beta") {
    throw new Error("RELEASE_CHANNEL must be stable or beta");
  }
  return channel;
}

function acceptsReleaseChannel(channel, advertised) {
  // Existing clients omit the field and belong to stable. Beta must opt in.
  return (advertised === undefined ? "stable" : advertised) === channel;
}

module.exports = {releaseChannel, acceptsReleaseChannel};
