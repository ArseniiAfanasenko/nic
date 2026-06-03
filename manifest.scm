(use-modules (guix profiles)
             (guix packages)
             (guix search-paths)
             (gnu packages musl))

;; Create a custom musl package that exports CC and LD_LIBRARY_PATH
(define musl-with-env-cc
  (package
    (inherit musl)
    (name "musl-with-env-cc")
    (native-search-paths
     (append (package-native-search-paths musl)
             (list (search-path-specification
                    (variable "CC")
                    (files '("bin/musl-gcc"))
                    (file-type 'regular)
                    (separator #f))
                   (search-path-specification
                    (variable "LD_LIBRARY_PATH")
                    (files '("lib"))
                    (file-type 'directory)
                    (separator ":")))))))

(packages->manifest (list musl-with-env-cc))
