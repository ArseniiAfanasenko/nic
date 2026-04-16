(use-modules (guix profiles)
             (guix packages)
             (guix search-paths)
             (gnu packages commencement)
             (gnu packages musl))

;; Create a custom GCC package that exports CC and LD_LIBRARY_PATH
(define gcc-with-env-cc
  (package
    (inherit gcc-toolchain)
    (name "gcc-toolchain")
    (native-search-paths
     (append (package-native-search-paths gcc-toolchain)
             (list (search-path-specification
                    (variable "CC")
                    (files '("bin/gcc"))
                    (file-type 'regular)
                    (separator #f))
                   (search-path-specification
                    (variable "LD_LIBRARY_PATH")
                    (files '("lib"))
                    (file-type 'directory)
                    (separator ":")))))))

(packages->manifest (list gcc-with-env-cc glibc))
