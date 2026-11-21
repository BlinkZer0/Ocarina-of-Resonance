import java.io.OutputStream;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.jar.JarFile;

class VerifyBundle {
    public static void main(String[] args) throws Exception {
        try (var bundle = new JarFile(Path.of(args[0]).toFile(), true)) {
            var sha256 = MessageDigest.getInstance("SHA-256");
            var entries = bundle.entries();
            int verified = 0;
            while (entries.hasMoreElements()) {
                var entry = entries.nextElement();
                if (entry.isDirectory() || entry.getName().matches("(?i)META-INF/[^/]+\\.(MF|SF|RSA|DSA|EC)")) continue;
                try (var data = bundle.getInputStream(entry)) {
                    data.transferTo(OutputStream.nullOutputStream());
                }
                var signers = entry.getCodeSigners();
                if (signers == null || signers.length != 1)
                    throw new SecurityException("Missing or ambiguous AAB signer for " + entry.getName());
                var certificate = signers[0].getSignerCertPath().getCertificates().getFirst();
                var fingerprint = HexFormat.of().formatHex(sha256.digest(certificate.getEncoded()));
                if (!fingerprint.equals(args[1]))
                    throw new SecurityException("The AAB certificate differs from the APK certificate.");
                verified++;
            }
            if (verified == 0) throw new SecurityException("The AAB has no signed content.");
        }
    }
}
